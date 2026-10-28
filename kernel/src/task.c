#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "kstring.h"
#include "pit.h"
#include "sync.h"
#include "task.h"

#define TASK_STACK_SIZE (16 * 1024)
#define TASK_NAME_MAX 16

typedef struct {
    uint64_t rsp;
    uint8_t *stack; // NULL for kernel_main, it runs on the boot.S stack
    task_state_t state;
    uint64_t wake_tick;
    uint32_t pid;
    int system;
    char name[TASK_NAME_MAX];
} task_t;

extern void task_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void task_trampoline(void);

static task_t tasks[TASK_MAX];
static int current_task = 0;
static int idle_slot = -1;
static uint32_t next_pid = 0;
static int scheduler_ready = 0;

static void idle_loop(void) {
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

// can't free a stack we're running on, so dead tasks are reaped by whoever
// yields next
static void reap_dead(void) {
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_DEAD && i != current_task) {
            kfree(tasks[i].stack);
            tasks[i].stack = NULL;
            tasks[i].state = TASK_UNUSED;
        }
    }
}

static void wake_sleepers(void) {
    uint64_t now = pit_get_ticks();

    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_SLEEPING && now >= tasks[i].wake_tick) {
            tasks[i].state = TASK_READY;
        }
    }
}

// round robin. idle only runs when nothing else can, otherwise it would steal
// one slice out of N even with work queued
static int pick_next(void) {
    for (int i = 1; i <= TASK_MAX; i++) {
        int slot = (current_task + i) % TASK_MAX;
        if (slot != idle_slot && tasks[slot].state == TASK_READY) {
            return slot;
        }
    }
    return idle_slot;
}

void task_init(void) {
    current_task = 0;
    tasks[0].rsp = 0; // filled in by the first task_switch
    tasks[0].stack = NULL;
    tasks[0].state = TASK_READY;
    tasks[0].pid = next_pid++;
    tasks[0].system = 1;
    kstrlcpy(tasks[0].name, "kernel_main", TASK_NAME_MAX);

    int pid = task_create("idle", idle_loop);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].pid == (uint32_t)pid) {
            idle_slot = i;
            tasks[i].system = 1;
        }
    }

    scheduler_ready = 1;
}

int task_create(const char *name, task_entry_t entry) {
    uint8_t *stack = kmalloc(TASK_STACK_SIZE);
    if (stack == NULL) {
        return -1;
    }

    // fake a stack that looks like it was left by task_switch: six zeroed
    // callee-saved regs, then the trampoline as return address, then the entry
    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE) - 8;
    for (int i = 0; i < 6; i++) {
        sp[i] = 0;
    }
    sp[6] = (uint64_t)task_trampoline;
    sp[7] = (uint64_t)entry;

    uint64_t flags = irq_save();

    int slot = -1;
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        irq_restore(flags);
        kfree(stack);
        return -1;
    }

    task_t *t = &tasks[slot];
    t->rsp = (uint64_t)sp;
    t->stack = stack;
    t->wake_tick = 0;
    t->pid = next_pid++;
    t->system = 0;
    kstrlcpy(t->name, name, TASK_NAME_MAX);
    t->state = TASK_READY;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

// called by tasks and by IRQ0. picking and switching happen with irqs off,
// whoever we switch to restores its own flags
void task_yield(void) {
    if (!scheduler_ready) {
        return;
    }

    uint64_t flags = irq_save();

    reap_dead();
    wake_sleepers();

    int prev = current_task;
    int next = pick_next();

    // nobody else ready and we can keep going
    if (next == idle_slot && tasks[prev].state == TASK_READY && prev != idle_slot) {
        next = prev;
    }

    if (next != prev) {
        current_task = next;
        task_switch(&tasks[prev].rsp, tasks[next].rsp);
    }

    irq_restore(flags);
}

void task_exit(void) {
    __asm__ volatile("cli");
    tasks[current_task].state = TASK_DEAD;
    task_yield();

    // not reached, DEAD tasks are never picked again
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static int slot_of(uint32_t pid) {
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

int task_kill(uint32_t pid) {
    uint64_t flags = irq_save();

    int slot = slot_of(pid);
    if (slot < 0 || tasks[slot].state == TASK_DEAD) {
        irq_restore(flags);
        return -1;
    }
    if (tasks[slot].system) {
        irq_restore(flags);
        return -2;
    }

    if (slot == current_task) {
        task_exit();
    }

    tasks[slot].state = TASK_DEAD;
    irq_restore(flags);
    return 0;
}

void task_protect(uint32_t pid) {
    uint64_t flags = irq_save();
    int slot = slot_of(pid);
    if (slot >= 0) {
        tasks[slot].system = 1;
    }
    irq_restore(flags);
}

void task_sleep_ms(uint64_t ms) {
    uint64_t ticks = ms * pit_get_frequency() / 1000;
    if (ticks == 0) {
        ticks = 1;
    }

    uint64_t flags = irq_save();
    tasks[current_task].wake_tick = pit_get_ticks() + ticks;
    tasks[current_task].state = TASK_SLEEPING;
    task_yield();
    irq_restore(flags);
}

void task_block(void) {
    uint64_t flags = irq_save();
    tasks[current_task].state = TASK_BLOCKED;
    task_yield();
    irq_restore(flags);
}

void task_wake(uint32_t pid) {
    uint64_t flags = irq_save();
    int slot = slot_of(pid);
    if (slot >= 0 && tasks[slot].state == TASK_BLOCKED) {
        tasks[slot].state = TASK_READY;
    }
    irq_restore(flags);
}

uint32_t task_current_pid(void) {
    return tasks[current_task].pid;
}

int task_alive(uint32_t pid) {
    uint64_t flags = irq_save();
    int slot = slot_of(pid);
    int alive = slot >= 0 && tasks[slot].state != TASK_DEAD;
    irq_restore(flags);
    return alive;
}

int task_slot_info(int slot, uint32_t *pid, task_state_t *state, const char **name, int *is_current) {
    if (slot < 0 || slot >= TASK_MAX || tasks[slot].state == TASK_UNUSED) {
        return 0;
    }

    *pid = tasks[slot].pid;
    *state = tasks[slot].state;
    *name = tasks[slot].name;
    *is_current = slot == current_task;
    return 1;
}
