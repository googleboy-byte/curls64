ENTRY(multiboot2_start)

SECTIONS
{
  /* Kernel code starts at 1MB (standard for 64-bit kernels and safe for Multiboot2) */
  . = 0x100000;

  /* Multiboot2 header must be in the first 32 KiB of the file */
  .multiboot2 ALIGN(8) : {
    *(.multiboot2)
  }

  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss  : { *(.bss*)  }

  /* Ktrace persistent memory - survives soft reboot */
  /* Keep it at a fixed physical location for now */
  . = 0x400000;
  .ktrace (NOLOAD) : {
    __ktrace_start = .;
    KEEP(*(.ktrace))
    __ktrace_end = .;
  }
}
