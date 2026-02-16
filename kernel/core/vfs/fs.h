#ifndef FS_H
#define FS_H

int remove_directory_recursive(const char *d);
void list_files(const char *dir);
void create_directory(const char *d);
void list_directories(void);
void cat_file(const char *f);
void do_make_file(const char *filename);
void do_redirect_output(const char *filename);
void import_text_drive(const char *textFile, const char *destTxt, int overrideClusters, int overrideSize);

#endif /* FS_H */
