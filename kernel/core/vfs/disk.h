#ifndef DISK_H
#define DISK_H

void read_disk_header(void);
void list_clusters_contents(void);
void print_disk_formatted(void);
void update_cluster_line(int clu, const char *hexData);
void flintstone_format_disk(const char *volumeName, int rowCount, int nibbleCount);
void format_disk_file(const char *diskFileName, const char *volumeName, int rowCount, int nibbleCount);

#endif /* DISK_H */
