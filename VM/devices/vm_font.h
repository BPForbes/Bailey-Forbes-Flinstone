#ifndef VM_FONT_H
#define VM_FONT_H

#define VM_FONT_W 8
#define VM_FONT_HEIGHT 8

/* Minimal 8x8 font (first 128 ASCII). Each char = 8 bytes, MSB left. */
extern const unsigned char vm_font_8x8[128][8];

#endif /* VM_FONT_H */
