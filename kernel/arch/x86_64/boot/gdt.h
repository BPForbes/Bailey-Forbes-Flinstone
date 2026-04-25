#ifndef X86_64_GDT_H
#define X86_64_GDT_H

/* Load the minimal flat GDT and reload all segment registers.
 * Must be called before idt_install(). */
void gdt_install(void);

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10

#endif /* X86_64_GDT_H */
