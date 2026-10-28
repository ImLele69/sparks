.set MAGIC,  0xe85250d6
.set ARCH,   0            // i386 protected mode

// the "a" flag matters: without it the section isn't allocatable, ld puts it at
// the end of the file and GRUB (which only scans the first 32KiB) stops finding
// the header as soon as the kernel gets big enough
.section .multiboot, "a"
.align 8
header_start:
    .long MAGIC
    .long ARCH
    .long header_end - header_start
    .long -(MAGIC + ARCH + (header_end - header_start))

    // framebuffer tag, 1024x768x32. marked optional so that if GRUB can't set
    // the mode we still boot in VGA text, fb_init will see the tag is missing
    .align 8
    .word 5, 1
    .long 20
    .long 1024, 768, 32

    .align 8
    .word 0, 0
    .long 8
header_end:

.section .bss
.align 4096
.global p4_table
p4_table:
    .skip 4096
p3_table:
    .skip 4096
p2_table:
    .skip 4096
stack_bottom:
    .skip 16384
stack_top:

.section .rodata
.align 8
gdt64:
    .quad 0
    .quad (1<<43) | (1<<44) | (1<<47) | (1<<53)    // 64-bit code segment
gdt64_pointer:
    .word . - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp

    mov $p3_table, %eax
    or $0x3, %eax
    mov %eax, (p4_table)

    mov $p2_table, %eax
    or $0x3, %eax
    mov %eax, (p3_table)

    // identity map the first GiB with 512 2MiB pages
    xor %ecx, %ecx
1:
    mov $0x200000, %eax
    mul %ecx
    or $0x83, %eax              // present | writable | huge
    mov %eax, p2_table(, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 1b

    mov %cr4, %eax
    or $(1<<5), %eax            // PAE
    mov %eax, %cr4

    mov $p4_table, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx       // EFER.LME
    rdmsr
    or $(1<<8), %eax
    wrmsr

    mov %cr0, %eax
    or $(1<<31), %eax           // PG
    mov %eax, %cr0

    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    xor %ax, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    // nothing has touched ebx since GRUB jumped here, it's still the mb2 info pointer
    mov %ebx, %edi
    call kernel_main

    cli
hang:
    hlt
    jmp hang
