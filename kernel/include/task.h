#ifndef TASK_H
#define TASK_H

#include <stdint.h>

typedef void (*task_entry_t)(void);

typedef enum {
    TASK_UNUSED,
    TASK_READY,
    TASK_SLEEPING,
    TASK_BLOCKED,
    TASK_DEAD,
} task_state_t;

#define TASK_MAX 32

void task_init(void);
// returns the pid, or -1 if we're out of slots or heap
int task_create(const char *name, task_entry_t entry);
void task_yield(void);
__attribute__((noreturn)) void task_exit(void);

// 0 ok, -1 no such pid, -2 system task (kernel_main, idle, wm)
int task_kill(uint32_t pid);
void task_protect(uint32_t pid);

void task_sleep_ms(uint64_t ms);
// check your wait condition with irqs off before calling this, or the
// task_wake can land in between and get lost
void task_block(void);
void task_wake(uint32_t pid);

uint32_t task_current_pid(void);
int task_alive(uint32_t pid);

// for ps. 0 if the slot is empty
int task_slot_info(int slot, uint32_t *pid, task_state_t *state, const char **name, int *is_current);

#endif
