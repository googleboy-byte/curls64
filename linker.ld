ENTRY(multiboot2_start)

SECTIONS
{
  /* Kernel code starts at 64KB (as loaded by Stage 2 / GRUB) */
  . = 0x10000;

  /* Multiboot2 header must be in the first 32 KiB of the file */
  .multiboot2 ALIGN(8) : {
    *(.multiboot2)
  }

  .text : { *(.text*) }
  .data : { *(.data*) }
  .bss  : { *(.bss*)  }

  /* Ktrace persistent memory - survives soft reboot */
  /* Moved to 4MB (safe within 16MB identity mapping) */
  . = 0x400000;
  .ktrace (NOLOAD) : {
    __ktrace_start = .;
    KEEP(*(.ktrace))
    __ktrace_end = .;
  }
}
