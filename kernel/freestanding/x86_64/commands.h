#ifndef FL_FREESTANDING_COMMANDS_H
#define FL_FREESTANDING_COMMANDS_H

void fl_fs_commands_init(void);
int fl_fs_commands_run(int session, const char *verb, const char **cursor);

#endif
