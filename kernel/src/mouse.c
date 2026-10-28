#include <stdint.h>

#include "fb.h"
#include "io.h"
#include "irq.h"
#include "mouse.h"
#include "pic.h"
#include "serial.h"
#include "sync.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL 0x02
#define STATUS_AUX_DATA 0x20

#define CMD_READ_CONFIG 0x20
#define CMD_WRITE_CONFIG 0x60
#define CMD_ENABLE_AUX 0xA8
#define CMD_WRITE_AUX 0xD4

#define CONFIG_AUX_IRQ 0x02
#define CONFIG_AUX_CLOCK_OFF 0x20

#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE_REPORTING 0xF4
#define MOUSE_ACK 0xFA

// packet byte 0
#define PKT_ALWAYS_ONE 0x08
#define PKT_X_SIGN 0x10
#define PKT_Y_SIGN 0x20
#define PKT_OVERFLOW 0xC0

// machines without a real 8042 just never answer, don't hang the boot on them
#define PS2_TIMEOUT 100000

static int32_t pos_x, pos_y;
static int32_t max_x, max_y;
static uint8_t buttons;

static uint8_t packet[3];
static int packet_len = 0;

// only button changes get queued, position is polled. a click that happens
// between two wm frames would be lost otherwise
#define EVENT_QUEUE_SIZE 32
static mouse_event_t events[EVENT_QUEUE_SIZE];
static int event_head = 0;
static int event_tail = 0;

static int wait_input_empty(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++) {
        if (!(inb(PS2_STATUS) & STATUS_INPUT_FULL)) {
            return 1;
        }
    }
    return 0;
}

static int wait_output_full(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++) {
        if (inb(PS2_STATUS) & STATUS_OUTPUT_FULL) {
            return 1;
        }
    }
    return 0;
}

static int controller_command(uint8_t cmd) {
    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_COMMAND, cmd);
    return 1;
}

static int controller_write_data(uint8_t data) {
    if (!wait_input_empty()) {
        return 0;
    }
    outb(PS2_DATA, data);
    return 1;
}

static int mouse_command(uint8_t cmd) {
    if (!controller_command(CMD_WRITE_AUX) || !controller_write_data(cmd)) {
        return 0;
    }
    return wait_output_full() && inb(PS2_DATA) == MOUSE_ACK;
}

static void mouse_irq_handler(registers_t *regs) {
    (void)regs;

    // the ACKs from mouse_init leave an IRQ12 pending while it's masked. it fires
    // on unmask with an empty buffer, and re-reading 0x60 would give us the stale
    // 0xFA, which has bit 3 set and looks like a packet start
    uint8_t status = inb(PS2_STATUS);
    if (!(status & STATUS_OUTPUT_FULL) || !(status & STATUS_AUX_DATA)) {
        return;
    }

    uint8_t data = inb(PS2_DATA);

    // resync: byte 0 always has bit 3 set. without this one lost byte shifts
    // every packet after it
    if (packet_len == 0 && !(data & PKT_ALWAYS_ONE)) {
        return;
    }

    packet[packet_len++] = data;
    if (packet_len < 3) {
        return;
    }
    packet_len = 0;

    if (packet[0] & PKT_OVERFLOW) {
        return;
    }

    // 9-bit deltas, sign bits live in byte 0
    int32_t dx = (int32_t)packet[1] - ((packet[0] & PKT_X_SIGN) ? 256 : 0);
    int32_t dy = (int32_t)packet[2] - ((packet[0] & PKT_Y_SIGN) ? 256 : 0);

    uint8_t new_buttons = packet[0] & 0x07;
    pos_x += dx;
    pos_y -= dy; // mouse y goes up

    if (pos_x < 0) pos_x = 0;
    if (pos_y < 0) pos_y = 0;
    if (pos_x > max_x) pos_x = max_x;
    if (pos_y > max_y) pos_y = max_y;

    if (new_buttons != buttons) {
        int next = (event_tail + 1) % EVENT_QUEUE_SIZE;
        if (next != event_head) {
            events[event_tail].x = pos_x;
            events[event_tail].y = pos_y;
            events[event_tail].buttons = new_buttons;
            event_tail = next;
        }
        buttons = new_buttons;
    }

    fb_cursor_move((uint32_t)pos_x, (uint32_t)pos_y);
}

// call with irqs off, otherwise the keyboard handler eats our ACKs off 0x60
int mouse_init(void) {
    while (inb(PS2_STATUS) & STATUS_OUTPUT_FULL) {
        inb(PS2_DATA);
    }

    if (!controller_command(CMD_ENABLE_AUX) || !controller_command(CMD_READ_CONFIG) || !wait_output_full()) {
        serial_write("mouse: no answer from the 8042\n");
        return 0;
    }

    uint8_t config = inb(PS2_DATA);
    config |= CONFIG_AUX_IRQ;
    config &= (uint8_t)~CONFIG_AUX_CLOCK_OFF;

    if (!controller_command(CMD_WRITE_CONFIG) || !controller_write_data(config)) {
        serial_write("mouse: config write failed\n");
        return 0;
    }

    if (!mouse_command(MOUSE_SET_DEFAULTS) || !mouse_command(MOUSE_ENABLE_REPORTING)) {
        serial_write("mouse: no ACK, giving up\n");
        return 0;
    }

    if (fb_available()) {
        max_x = (int32_t)fb_width() - 1;
        max_y = (int32_t)fb_height() - 1;
    } else {
        // text mode: nothing is drawn but position and buttons still work
        max_x = 79;
        max_y = 24;
    }
    pos_x = max_x / 2;
    pos_y = max_y / 2;
    fb_cursor_move((uint32_t)pos_x, (uint32_t)pos_y);

    irq_register_handler(12, mouse_irq_handler);
    pic_clear_mask(2); // cascade
    pic_clear_mask(12);

    serial_write("mouse: ps/2 on IRQ12\n");
    return 1;
}

int32_t mouse_x(void) {
    return pos_x;
}

int32_t mouse_y(void) {
    return pos_y;
}

uint8_t mouse_buttons(void) {
    return buttons;
}

int mouse_poll_event(mouse_event_t *ev) {
    uint64_t flags = irq_save();

    if (event_head == event_tail) {
        irq_restore(flags);
        return 0;
    }

    *ev = events[event_head];
    event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
    irq_restore(flags);
    return 1;
}
