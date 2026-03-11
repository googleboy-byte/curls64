ENTRY(_start)

SECTIONS
{
    /* Canonical user-space address */
    . = 0x0000000000400000;

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
