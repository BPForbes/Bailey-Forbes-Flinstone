#ifndef FL_FREESTANDING_KEYBOARD_H
#define FL_FREESTANDING_KEYBOARD_H

void fl_fs_kbd_init(void);
void fl_fs_kbd_irq(void);
int fl_fs_kbd_getc(char *out);

#endif
