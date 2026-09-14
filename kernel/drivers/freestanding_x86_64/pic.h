#ifndef FL_FREESTANDING_PIC_H
#define FL_FREESTANDING_PIC_H

void fl_fs_pic_init(void);
void fl_fs_pic_unmask(int irq);
void fl_fs_pic_eoi(int irq);

#endif
