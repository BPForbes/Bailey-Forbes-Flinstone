#ifndef FL_LAB_KQ_H
#define FL_LAB_KQ_H

/*
 * KeyQuorum co-signed delivery in the WebAssembly lab shell (built with FL_LAB_KQ; the QEMU
 * image and the host smoke binary are built without it). See lab_kq.c.
 */

/** Where asynchronous lines go (the shell's own output, so they reach the terminal). */
void fl_lab_kq_init(void (*emit)(const char *line));

/** server file send -t [--permitted] <path> -user <name> */
int fl_lab_kq_send(int session, const char *path, const char *user, int permitted);
/** server file permit <path>: the signed-in supervisor countersigns the head. */
int fl_lab_kq_permit(int session, const char *path);
/** server file log <path>: the history of a tracked copy, offline. */
int fl_lab_kq_log(int session, const char *path);
/** server file cosign [<delivery>] */
int fl_lab_kq_cosign(int session, const char *hex);
/** server file open <delivery> */
int fl_lab_kq_open(int session, const char *hex);

#endif /* FL_LAB_KQ_H */
