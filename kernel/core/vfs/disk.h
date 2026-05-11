#ifndef DISK_H
#define DISK_H

void read_disk_header(void);
void list_clusters_contents(void);
void print_disk_formatted(void);
void update_cluster_line(int clu, const char *hexData);
void flintstone_format_disk(const char *volumeName, int rowCount, int nibbleCount);
void format_disk_file(const char *diskFileName, const char *volumeName, int rowCount, int nibbleCount);

/* Create a FAT32 image at path if missing (512-byte clusters only). */
void disk_ensure_default_fat32(const char *path, int clusters, int bytes_per_cluster);

/*
 * Shell command history embedded in the current volume (no separate host .txt).
 * FAT32: fixed tail region inside FLINT.DAT. Legacy hex: lines "#SHELL:<cmd>" after cluster lines.
 */
int disk_embedded_shell_history_append(const char *cmd);
char *disk_embedded_shell_history_read_all(void);
void disk_embedded_shell_history_clear(void);
void disk_embedded_shell_history_print_list(void);

#endif /* DISK_H */
