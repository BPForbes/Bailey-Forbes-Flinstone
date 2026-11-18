ENTRY(kernel_entry)
SECTIONS {
  . = 0x10000;
  __kernel_start = .;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) *(.got*) }
  __image_end = .;
  /* Never loaded; entry.asm zeroes it at FL_KERNEL_BSS_ADDR, so code alone stays below FL_LOW_BUDGET. */
  .bss 0x100000 (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
  /DISCARD/ : { *(.comment*) *(.eh_frame*) *(.note*) }
}
