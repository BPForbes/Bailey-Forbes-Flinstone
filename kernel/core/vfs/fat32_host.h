#ifndef FAT32_HOST_H
#define FAT32_HOST_H

#include <stdint.h>

/* Parsed / cached host FAT32 super-floppy image (VBR at LBA 0, no MBR). */
typedef struct {
    int valid;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t sectors_per_fat32;
    uint32_t total_sectors32;
    uint32_t root_cluster;
    uint32_t first_data_sector; /* LBA */
    uint32_t flint_first_cluster; /* first cluster of FLINT.DAT (shell data) */
    int shell_clusters;           /* logical clusters in FLINT.DAT */
    int bytes_per_cluster;        /* BPS * SPC */
    uint32_t data_cluster_count;  /* clusters in data region (cluster numbers 2 .. count+1) */
} Fat32HostVol;

extern Fat32HostVol g_fat32_host_vol;

void fat32_host_invalidate(void);

/* Return 1 if sec512 is a plausible FAT32 boot sector (FLINT volumes). */
int fat32_host_probe_sector(const uint8_t *sec);

/* Return 1 if first sector looks like our FAT32 super-floppy volume. */
int fat32_host_probe_fd(int fd);

/* Load BPB + locate FLINT.DAT from root; sets g_fat32_host_vol and g_cluster_size / g_total_clusters. */
int fat32_host_load_from_fd(int fd);

/*
 * Create a FAT32-compliant super-floppy image file.
 * shell_clusters: logical cluster count for FLINT.DAT (cluster size = bps*spc).
 * volume_label: up to 11 chars (ASCII), padded with spaces in the BPB.
 * Returns 0 on success.
 */
int fat32_host_format_image(const char *path, const char *volume_label,
                            int shell_clusters, int bytes_per_cluster,
                            const char *flint_volume_name);

/* Byte offset within the image file for shell cluster index (0 .. shell_clusters-1). */
int fat32_host_shell_cluster_byte_offset(int shell_clu, uint64_t *out_off);

/*
 * Store arbitrary binary files in the FAT32 root directory (alongside FLINT.DAT).
 * Names are FAT 8.3 (long names are mangled to NAME~N.EXT). Requires loaded volume.
 * Paths are host files; disk image is current_disk_file (see common.h).
 */
int fat32_host_file_put(const char *host_src_path, const char *name_on_disk_or_null);
int fat32_host_file_get(const char *name_on_disk_83, const char *host_dst_path);
int fat32_host_file_del(const char *name_on_disk_83);
void fat32_host_file_list(void);

#endif /* FAT32_HOST_H */
