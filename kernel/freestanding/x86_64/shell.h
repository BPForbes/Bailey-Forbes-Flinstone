#ifndef FL_FREESTANDING_SHELL_H
#define FL_FREESTANDING_SHELL_H

void fl_fs_shell_init(void);
void fl_fs_shell_input(char c);
void fl_fs_shell_run(void);
/** Runs the background tick (workqueue) once; the shell loop calls it after a timer interrupt. */
void fl_fs_bg_tick(void);

#endif
