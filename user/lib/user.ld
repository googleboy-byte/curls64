ENTRY(_start)
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
    . = 0x08048000;

    .text : AT(ADDR(.text))
    {
        *(.text)
    }

    .data : AT(ADDR(.data))
    {
        *(.data)
        *(.rodata)
    }

    .bss : AT(ADDR(.bss))
    {
        *(.bss)
    }
}
