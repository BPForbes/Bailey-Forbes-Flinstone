#ifndef FL_FREESTANDING_LABDISK_H
#define FL_FREESTANDING_LABDISK_H

void fl_fs_labdisk_init(void);
int fl_fs_labdisk_create(const char *volume, unsigned rows, unsigned nibbles);
int fl_fs_labdisk_format(const char *backing, const char *volume, unsigned rows, unsigned nibbles);
int fl_fs_labdisk_set(const char *backing);
int fl_fs_labdisk_initgeom(unsigned count, unsigned size);
void fl_fs_labdisk_print(void (*emit)(const char *), void (*emit_uint)(unsigned));
void fl_fs_labdisk_list(void (*emit)(const char *), void (*emit_uint)(unsigned));
int fl_fs_labdisk_write_cluster(unsigned idx, const char *text);
int fl_fs_labdisk_del_cluster(unsigned idx);
int fl_fs_labdisk_add_cluster(const char *text);
int fl_fs_labdisk_put(const char *name, const char *data);
int fl_fs_labdisk_get(const char *name, char *out, unsigned cap);
int fl_fs_labdisk_del_file(const char *name);
int fl_fs_labdisk_mkdir(const char *name);
void fl_fs_labdisk_files(void (*visit)(const char *name, int is_dir, unsigned size, void *ctx), void *ctx);
int fl_fs_labdisk_search(const char *text,
                         void (*visit)(const char *path, void *ctx), void *ctx);
void fl_fs_labdisk_usage(unsigned *bytes, unsigned *clusters);

#endif
