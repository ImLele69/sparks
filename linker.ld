ENTRY(_start)

SECTIONS
{
    . = 1M;

    _kernel_start = .;

    .multiboot :
    {
        *(.multiboot)
    }

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = .;
}
