#ifndef CLUSTER_H
#define CLUSTER_H

char *convert_data_to_hex(const char *data, int inputIsText, int clusterSize);
void process_write_cluster(int clu, const char *data, int inputIsText);
void calculate_storage_breakdown_for_cluster(int clu);
void delete_cluster(int clu);
void show_disk_detail_for_cluster(int clu);
char *convert_hex_to_ascii(const char *hexData, int clusterSize);

#endif /* CLUSTER_H */
