.code64
.section .text

// void task_switch(uint64_t *old_rsp, uint64_t new_rsp)
//
// only the callee-saved regs need saving, the compiler already spilled the rest
// around the call. irqs are off here (task_yield did irq_save) and we don't turn
// them back on: the resumed task does that in its own irq_restore, or with iretq
// if it was preempted from IRQ0
.global task_switch
task_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, (%rdi)
    mov %rsi, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp

    ret

// first run of a task: task_create left the entry point right above the return
// address that brought us here
.global task_trampoline
task_trampoline:
    pop %rax
    // a brand new task has no task_yield up the stack to restore irqs for it
    sti
    call *%rax
    call task_exit
