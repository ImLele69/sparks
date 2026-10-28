#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "irq.h"
#include "keyboard.h"
#include "pic.h"
#include "sync.h"
#include "task.h"

#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64
#define KBD_STATUS_OUTPUT_FULL 0x01
#define KBD_STATUS_AUX_DATA 0x20

#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_LSHIFT_RELEASE 0xAA
#define SCANCODE_RSHIFT_RELEASE 0xB6

#define KBD_BUFFER_SIZE 256

static int shift_pressed = 0;
static int extended_prefix = 0;

// ring buffer, the irq pushes at tail and keyboard_getchar pops at head
static char kbd_buffer[KBD_BUFFER_SIZE];
static size_t kbd_head = 0;
static size_t kbd_tail = 0;

// pid blocked in keyboard_getchar, or -1
static int64_t waiting_pid = -1;

static void kbd_buffer_push(char c) {
    size_t next = (kbd_tail + 1) % KBD_BUFFER_SIZE;

    if (next == kbd_head) {
        return; // full, drop it
    }

    kbd_buffer[kbd_tail] = c;
    kbd_tail = next;

    if (waiting_pid >= 0) {
        task_wake((uint32_t)waiting_pid);
    }
}

// US layout, scancode set 1
static const char ascii_table[128] = {
    [0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/', [0x37] = '*',
    [0x39] = ' ',
    [0x4A] = '-', [0x4E] = '+',
};

static const char ascii_table_shift[128] = {
    [0x01] = 27, [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(',
    [0x0B] = ')', [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
    [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>',
    [0x35] = '?', [0x37] = '*',
    [0x39] = ' ',
    [0x4A] = '-', [0x4E] = '+',
};

// keys behind the 0xE0 prefix
static char extended_to_key(uint8_t scancode) {
    switch (scancode) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x53: return KEY_DELETE;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x35: return '/';  // keypad
        case 0x1C: return '\n'; // keypad enter
        default: return 0;
    }
}

static void keyboard_irq_handler(registers_t *regs) {
    (void)regs;

    // 0x60 is shared with the mouse, leave aux bytes for IRQ12
    uint8_t status = inb(KBD_STATUS_PORT);
    if (!(status & KBD_STATUS_OUTPUT_FULL) || (status & KBD_STATUS_AUX_DATA)) {
        return;
    }

    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode == 0xE0) {
        extended_prefix = 1;
        return;
    }

    if (extended_prefix) {
        extended_prefix = 0;

        if (scancode & 0x80) {
            return; // release
        }

        char c = extended_to_key(scancode);
        if (c) {
            kbd_buffer_push(c);
        }
        return;
    }

    if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
        shift_pressed = 1;
        return;
    }

    if (scancode == SCANCODE_LSHIFT_RELEASE || scancode == SCANCODE_RSHIFT_RELEASE) {
        shift_pressed = 0;
        return;
    }

    if (scancode & 0x80) {
        return;
    }

    char c = shift_pressed ? ascii_table_shift[scancode] : ascii_table[scancode];
    if (c) {
        kbd_buffer_push(c);
    }
}

int keyboard_has_char(void) {
    return kbd_head != kbd_tail;
}

// the empty check and task_block must be in the same critical section, or an
// IRQ1 in between would wake us while still READY and the wakeup is lost
char keyboard_getchar(void) {
    for (;;) {
        uint64_t flags = irq_save();

        if (kbd_head != kbd_tail) {
            char c = kbd_buffer[kbd_head];
            kbd_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
            waiting_pid = -1;
            irq_restore(flags);
            return c;
        }

        waiting_pid = task_current_pid();
        task_block();
        irq_restore(flags);
    }
}

void keyboard_init(void) {
    irq_register_handler(1, keyboard_irq_handler);
    pic_clear_mask(1);
}
