#ifndef FL_LAB_KQ_H
#define FL_LAB_KQ_H


void fl_lab_kq_bind_identity(void);

void fl_lab_kq_init(void (*emit)(const char *line));

int fl_lab_kq_send(int session, const char *path, const char *user, int permitted);
int fl_lab_kq_permit(int session, const char *path);
int fl_lab_kq_log(int session, const char *path);
int fl_lab_kq_cosign(int session, const char *hex);
int fl_lab_kq_open(int session, const char *hex);

#endif /* FL_LAB_KQ_H */
