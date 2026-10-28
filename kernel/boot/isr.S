// entry stubs for the 32 CPU exceptions and the 16 PIC irqs. every stub pushes
// (error code, vector) so both common paths see the same registers_t layout

.macro ISR_NOERR num
.global isr\num
isr\num:
    push $0             // fake error code
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    push $\num          // the cpu already pushed the real one
    jmp isr_common_stub
.endm

.macro IRQ num
.global irq\num
irq\num:
    push $0
    push $(32 + \num)   // PIC is remapped to 0x20
    jmp irq_common_stub
.endm

.macro PUSH_REGS
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
.endm

.macro POP_REGS
    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15
.endm

.macro COMMON_STUB name, handler
\name:
    PUSH_REGS
    mov %rsp, %rdi      // registers_t *
    call \handler
    POP_REGS
    add $16, %rsp       // drop vector + error code
    iretq
.endm

.code64
.section .text

.irp i, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,29,31
    ISR_NOERR \i
.endr

.irp i, 8,10,11,12,13,14,17,21,30
    ISR_ERR \i
.endr

.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    IRQ \i
.endr

COMMON_STUB isr_common_stub, isr_handler
COMMON_STUB irq_common_stub, irq_handler

.section .rodata
.align 8
.global isr_stub_table
isr_stub_table:
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad isr\i
.endr

.global irq_stub_table
irq_stub_table:
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    .quad irq\i
.endr
