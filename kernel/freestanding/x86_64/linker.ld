ENTRY(kernel_entry)
SECTIONS {
  . = 0x10000;
  __kernel_start = .;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) __bss_end = .; }
  /DISCARD/ : { *(.comment*) *(.eh_frame*) *(.note*) }
}
