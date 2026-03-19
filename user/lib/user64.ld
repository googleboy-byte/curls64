ENTRY(_start)

SECTIONS
{
    /* Canonical user-space address */
    . = 0x0000000008000000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
