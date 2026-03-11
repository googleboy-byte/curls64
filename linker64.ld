ENTRY(multiboot2_start)

SECTIONS
{
  /* Kernel code starts at 1MB (standard for 64-bit kernels and safe for Multiboot2) */
  . = 0x100000;

  /* Multiboot2 header must be in the first 32 KiB of the file */
  .multiboot2 ALIGN(8) : {
    *(.multiboot2)
  }

  .text : ALIGN(4K) { *(.text*) }
  .rodata : ALIGN(4K) { *(.rodata*) }
  .data : ALIGN(4K) { *(.data*) }
  .bss  : ALIGN(4K) { *(.bss*) *(COMMON) }

  . = ALIGN(4K);
  _kernel_end = .;

  /* Ktrace persistent memory - survives soft reboot */
  /* Keep it at a fixed physical location for now */
  . = 0x400000;
  .ktrace (NOLOAD) : {
    __ktrace_start = .;
    KEEP(*(.ktrace))
    __ktrace_end = .;
  }

  /* Define the end of the kernel */
  end = .;
}
