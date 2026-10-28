#include <stddef.h>
#include <stdint.h>

#include "console.h"
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "mouse.h"
#include "pit.h"
#include "pmm.h"
#include "shell.h"
#include "task.h"

#define LINE_MAX 128
#define HISTORY_SIZE 16

typedef struct {
    const char *name;
    const char *usage;
    void (*run)(char *args);
} command_t;

static char history[HISTORY_SIZE][LINE_MAX];
static int history_count = 0;

// cuts line after the first word and returns the rest, possibly ""
static char *split_first_word(char *line) {
    char *p = line;

    while (*p && *p != ' ') {
        p++;
    }
    if (*p == ' ') {
        *p++ = '\0';
        while (*p == ' ') {
            p++;
        }
    }
    return p;
}

static int parse_uint(const char *s, uint32_t *out) {
    uint64_t v = 0;

    if (*s == '\0') {
        return 0;
    }
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFu) {
            return 0;
        }
    }

    *out = (uint32_t)v;
    return 1;
}

// test programs for the scheduler, they run until killed

static void task_counter(void) {
    for (uint64_t i = 0;; i++) {
        console_write("[counter] ");
        console_write_dec(i);
        console_write("\n");
        task_sleep_ms(500);
    }
}

static void task_busy(void) {
    for (uint64_t i = 0;; i++) {
        // never yields or sleeps, only the timer can take the cpu away
        for (volatile uint64_t j = 0; j < 50000000; j++) {
        }
        console_write("[busy] still alive, ");
        console_write_dec(i);
        console_write("\n");
    }
}

static void task_heap_stress(void) {
    for (uint64_t i = 0;; i++) {
        void *a = kmalloc(32);
        void *b = kmalloc(64);
        kfree(a);
        void *c = kmalloc(16);
        kfree(b);
        kfree(c);

        if (i % 20 == 0) {
            console_write("[heap] still alive, ");
            console_write_dec(i);
            console_write("\n");
        }
        task_sleep_ms(20);
    }
}

static const struct {
    const char *name;
    task_entry_t entry;
} spawnable[] = {
    {"counter", task_counter},
    {"busy", task_busy},
    {"heap", task_heap_stress},
};

static void cmd_help(char *args);

static void cmd_clear(char *args) {
    (void)args;
    console_clear();
}

static void cmd_echo(char *args) {
    console_write(args);
    console_write("\n");
}

static const char *state_name(task_state_t state, int is_current) {
    if (is_current) {
        return "running ";
    }
    switch (state) {
        case TASK_READY: return "ready   ";
        case TASK_SLEEPING: return "sleeping";
        case TASK_BLOCKED: return "blocked ";
        case TASK_DEAD: return "dead    ";
        default: return "?       ";
    }
}

static void cmd_ps(char *args) {
    (void)args;
    console_write("PID  STATE     NAME\n");
    for (int slot = 0; slot < TASK_MAX; slot++) {
        uint32_t pid;
        task_state_t state;
        const char *name;
        int is_current;

        if (!task_slot_info(slot, &pid, &state, &name, &is_current)) {
            continue;
        }

        console_write_dec(pid);
        console_write(pid < 10 ? "    " : pid < 100 ? "   " : "  ");
        console_write(state_name(state, is_current));
        console_write("  ");
        console_write(name);
        console_write("\n");
    }
}

static void cmd_spawn(char *args) {
    char *name = split_first_word(args);
    task_entry_t entry = NULL;

    for (size_t i = 0; i < sizeof(spawnable) / sizeof(spawnable[0]); i++) {
        if (kstreq(args, spawnable[i].name)) {
            entry = spawnable[i].entry;
        }
    }
    if (entry == NULL) {
        console_write("usage: spawn <counter|busy|heap> [name]\n");
        return;
    }
    if (*name == '\0') {
        name = args;
    }

    int pid = task_create(name, entry);
    if (pid < 0) {
        console_write("spawn: out of task slots or heap\n");
        return;
    }

    console_write(name);
    console_write(" started, pid ");
    console_write_dec((uint64_t)pid);
    console_write("\n");
}

static void cmd_kill(char *args) {
    uint32_t pid;

    if (!parse_uint(args, &pid)) {
        console_write("usage: kill <pid>\n");
        return;
    }

    switch (task_kill(pid)) {
        case -1: console_write("kill: no such task\n"); break;
        case -2: console_write("kill: refusing to kill a system task\n"); break;
        default: break;
    }
}

static void cmd_meminfo(char *args) {
    (void)args;
    uint64_t total = pmm_total_pages();
    uint64_t free = pmm_free_pages();

    console_write("total ");
    console_write_dec(total);
    console_write(" pages, free ");
    console_write_dec(free);
    console_write(" (");
    console_write_dec(free * 4 / 1024);
    console_write(" MiB)\n");
}

static void cmd_memmap(char *args) {
    (void)args;
    pmm_print_mmap();
}

static void cmd_uptime(char *args) {
    (void)args;
    console_write_dec(pit_get_uptime_ms());
    console_write(" ms\n");
}

static void cmd_mouse(char *args) {
    (void)args;
    uint8_t b = mouse_buttons();

    console_write("x ");
    console_write_dec((uint64_t)mouse_x());
    console_write(" y ");
    console_write_dec((uint64_t)mouse_y());
    console_write(" buttons ");
    console_write(b & MOUSE_LEFT ? "L" : "-");
    console_write(b & MOUSE_MIDDLE ? "M" : "-");
    console_write(b & MOUSE_RIGHT ? "R" : "-");
    console_write("\n");
}

static const command_t commands[] = {
    {"help", "", cmd_help},
    {"clear", "", cmd_clear},
    {"echo", "<text>", cmd_echo},
    {"ps", "", cmd_ps},
    {"spawn", "<counter|busy|heap> [name]", cmd_spawn},
    {"kill", "<pid>", cmd_kill},
    {"meminfo", "", cmd_meminfo},
    {"memmap", "", cmd_memmap},
    {"uptime", "", cmd_uptime},
    {"mouse", "", cmd_mouse},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(char *args) {
    (void)args;
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        console_write("  ");
        console_write(commands[i].name);
        console_write(" ");
        console_write(commands[i].usage);
        console_write("\n");
    }
}

static void run_command(char *line) {
    char *args = split_first_word(line);

    if (line[0] == '\0') {
        return;
    }
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (kstreq(line, commands[i].name)) {
            commands[i].run(args);
            return;
        }
    }

    console_write(line);
    console_write(": command not found\n");
}

static void history_push(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    if (history_count == HISTORY_SIZE) {
        for (int i = 1; i < HISTORY_SIZE; i++) {
            kstrlcpy(history[i - 1], history[i], LINE_MAX);
        }
        history_count--;
    }
    kstrlcpy(history[history_count++], line, LINE_MAX);
}

// erase what's on screen and type text in its place
static void redraw_line(char *buf, size_t *len, const char *text, size_t max_len) {
    while (*len > 0) {
        console_putc('\b');
        (*len)--;
    }

    size_t i = 0;
    while (text[i] != '\0' && i + 1 < max_len) {
        buf[i] = text[i];
        console_putc(text[i]);
        i++;
    }
    *len = i;
}

// reprint buf[start..len], pad with `extra` spaces to cover leftovers, then
// walk the cursor `back` columns left
static void redraw_tail(const char *buf, size_t start, size_t len, size_t extra, size_t back) {
    for (size_t i = start; i < len; i++) {
        console_putc(buf[i]);
    }
    for (size_t i = 0; i < extra; i++) {
        console_putc(' ');
    }
    for (size_t i = 0; i < back; i++) {
        console_cursor_left();
    }
}

static void read_line(char *buf, size_t max_len) {
    size_t len = 0;
    size_t cursor = 0;
    int browse_idx = history_count; // == history_count means not browsing

    for (;;) {
        char c = console_getchar();

        switch (c) {
            case KEY_CANCEL:
                len = 0;
                goto done;

            case '\n':
                console_putc('\n');
                goto done;

            case '\b':
                if (cursor > 0) {
                    for (size_t i = cursor; i < len; i++) {
                        buf[i - 1] = buf[i];
                    }
                    cursor--;
                    len--;
                    console_cursor_left();
                    redraw_tail(buf, cursor, len, 1, len - cursor + 1);
                }
                break;

            case KEY_DELETE:
                if (cursor < len) {
                    for (size_t i = cursor; i < len - 1; i++) {
                        buf[i] = buf[i + 1];
                    }
                    len--;
                    redraw_tail(buf, cursor, len, 1, len - cursor + 1);
                }
                break;

            case KEY_LEFT:
                if (cursor > 0) {
                    cursor--;
                    console_cursor_left();
                }
                break;

            case KEY_RIGHT:
                if (cursor < len) {
                    cursor++;
                    console_cursor_right();
                }
                break;

            case KEY_HOME:
                for (; cursor > 0; cursor--) {
                    console_cursor_left();
                }
                break;

            case KEY_END:
                for (; cursor < len; cursor++) {
                    console_cursor_right();
                }
                break;

            case KEY_UP:
                if (browse_idx > 0) {
                    browse_idx--;
                    redraw_line(buf, &len, history[browse_idx], max_len);
                    cursor = len;
                }
                break;

            case KEY_DOWN:
                if (browse_idx < history_count) {
                    browse_idx++;
                    redraw_line(buf, &len, browse_idx == history_count ? "" : history[browse_idx], max_len);
                    cursor = len;
                }
                break;

            default:
                if (len + 1 < max_len) {
                    for (size_t i = len; i > cursor; i--) {
                        buf[i] = buf[i - 1];
                    }
                    buf[cursor] = c;
                    len++;
                    redraw_tail(buf, cursor, len, 0, len - cursor - 1);
                    cursor++;
                }
                break;
        }
    }

done:
    buf[len] = '\0';
    history_push(buf);
}

void shell_run(void) {
    char line[LINE_MAX];

    console_write("sparks shell, type 'help'\n");
    for (;;) {
        console_write("sparks> ");
        read_line(line, LINE_MAX);
        run_command(line);
    }
}
