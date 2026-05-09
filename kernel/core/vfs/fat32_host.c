/*
 * Host FAT32 super-floppy image (VBR at LBA 0, no partition table).
 * Layout matches common FAT32 boot sector / FAT / cluster addressing so images
 * can be inspected with standard tools (e.g. dosfsck, file(1), loop mount).
 *
 * Shell logical clusters live in a single preallocated root file FLINT.DAT
 * starting at a fixed first cluster (3); cluster 2 is the root directory.
 */

#include "fat32_host.h"
#include "mem_asm.h"
#include "common.h"
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

Fat32HostVol g_fat32_host_vol;

void fat32_host_invalidate(void) {
    memset(&g_fat32_host_vol, 0, sizeof(g_fat32_host_vol));
}

static void st_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void st_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint16_t ld_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ld_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define FAT32_MIN_USABLE_CLUSTERS 65525u

static int fat32_iterate_layout(uint32_t total_sectors, uint16_t bps, uint8_t spc, uint16_t rsv,
                                uint8_t nfats, uint32_t *fat_sec_out, uint32_t *cluster_count_out) {
    uint32_t fat = 1u;
    for (int iter = 0; iter < 128; iter++) {
        if (total_sectors <= rsv + nfats * fat)
            return -1;
        uint32_t data_sec = total_sectors - (uint32_t)rsv - nfats * fat;
        uint32_t cc = data_sec / (uint32_t)spc;
        uint64_t need_bytes = ((uint64_t)cc + 2u) * 4u;
        uint32_t fat_new = (uint32_t)((need_bytes + (uint64_t)bps - 1u) / (uint64_t)bps);
        if (fat_new == fat) {
            *fat_sec_out = fat;
            *cluster_count_out = cc;
            return 0;
        }
        fat = fat_new;
    }
    return -1;
}

int fat32_host_probe_fd(int fd) {
    uint8_t sec[512];
    if (pread(fd, sec, sizeof(sec), 0) != (ssize_t)sizeof(sec))
        return 0;
    if (sec[0] != 0xEB && sec[0] != 0xE9)
        return 0;
    if (memcmp(sec + 0x52, "FAT32   ", 8) != 0)
        return 0;
    uint16_t bps = ld_le16(sec + 0x0B);
    if (bps != 512u)
        return 0;
    return 1;
}

int fat32_host_load_from_fd(int fd) {
    uint8_t sec[512];
    if (pread(fd, sec, sizeof(sec), 0) != (ssize_t)sizeof(sec))
        return -1;
    if (!fat32_host_probe_fd(fd))
        return -1;

    uint16_t bps = ld_le16(sec + 0x0B);
    uint8_t spc = sec[0x0D];
    uint16_t rsv = ld_le16(sec + 0x0E);
    uint8_t nf = sec[0x10];
    uint32_t tot = ld_le32(sec + 0x20);
    uint32_t fat32sz = ld_le32(sec + 0x24);
    uint32_t rootclus = ld_le32(sec + 0x2C);
    if (bps == 0 || spc == 0 || nf == 0 || tot == 0 || fat32sz == 0)
        return -1;

    uint32_t first_data_sec = (uint32_t)rsv + (uint32_t)nf * fat32sz;

    /* Read root directory (one cluster) */
    uint32_t bpc = (uint32_t)bps * (uint32_t)spc;
    uint8_t *root = malloc(bpc);
    if (!root)
        return -1;
    uint64_t root_off = (uint64_t)first_data_sec * (uint64_t)bps +
                        (uint64_t)(rootclus - 2u) * (uint64_t)spc * (uint64_t)bps;
    if (pread(fd, root, bpc, (off_t)root_off) != (ssize_t)bpc) {
        free(root);
        return -1;
    }

    /* Find FLINT.DAT directory entry */
    int found = -1;
    for (uint32_t off = 0; off + 32 <= bpc; off += 32) {
        uint8_t *e = root + off;
        if (e[0] == 0 || e[0] == 0xE5)
            continue;
        if (memcmp(e, "FLINT   DAT", 11) == 0) {
            found = (int)off;
            break;
        }
    }
    if (found < 0) {
        free(root);
        return -1;
    }
    uint8_t *ent = root + (uint32_t)found;
    uint32_t lo = ld_le16(ent + 0x1A);
    uint32_t hi = ld_le16(ent + 0x14);
    uint32_t first = lo | (hi << 16);
    uint32_t fsz = ld_le32(ent + 0x1C);
    free(root);

    if (first < 3u || fsz == 0u || (fsz % bpc) != 0u)
        return -1;
    int shell_clusters = (int)(fsz / bpc);

    memset(&g_fat32_host_vol, 0, sizeof(g_fat32_host_vol));
    g_fat32_host_vol.valid = 1;
    g_fat32_host_vol.bytes_per_sector = bps;
    g_fat32_host_vol.sectors_per_cluster = spc;
    g_fat32_host_vol.reserved_sectors = rsv;
    g_fat32_host_vol.num_fats = nf;
    g_fat32_host_vol.sectors_per_fat32 = fat32sz;
    g_fat32_host_vol.total_sectors32 = tot;
    g_fat32_host_vol.root_cluster = rootclus;
    g_fat32_host_vol.first_data_sector = first_data_sec;
    g_fat32_host_vol.flint_first_cluster = first;
    g_fat32_host_vol.shell_clusters = shell_clusters;
    g_fat32_host_vol.bytes_per_cluster = (int)bpc;

    g_cluster_size = (int)bpc;
    g_total_clusters = shell_clusters;
    return 0;
}

int fat32_host_shell_cluster_byte_offset(int shell_clu, uint64_t *out_off) {
    if (!g_fat32_host_vol.valid || shell_clu < 0 || shell_clu >= g_total_clusters)
        return -1;
    uint32_t cl = g_fat32_host_vol.flint_first_cluster + (uint32_t)shell_clu;
    if (cl < 2u)
        return -1;
    uint64_t sec = (uint64_t)g_fat32_host_vol.first_data_sector +
                   (uint64_t)(cl - 2u) * (uint64_t)g_fat32_host_vol.sectors_per_cluster;
    *out_off = sec * (uint64_t)g_fat32_host_vol.bytes_per_sector;
    return 0;
}

static void fat32_set_label(uint8_t *dst11, const char *label) {
    size_t n = strlen(label ? label : "");
    if (n > 11u)
        n = 11u;
    memcpy(dst11, label, n);
    for (size_t i = n; i < 11u; i++)
        dst11[i] = ' ';
}

int fat32_host_format_image(const char *path, const char *volume_label, int shell_clusters,
                            int bytes_per_cluster, const char *flint_volume_name) {
    if (shell_clusters <= 0 || bytes_per_cluster <= 0)
        return -1;
    if (bytes_per_cluster % 512 != 0)
        return -1;
    const uint16_t bps = 512u;
    if (bytes_per_cluster % (int)bps != 0)
        return -1;
    uint8_t spc = (uint8_t)((unsigned)bytes_per_cluster / (unsigned)bps);
    if ((int)spc * (int)bps != bytes_per_cluster)
        return -1;

    uint32_t min_cc = (uint32_t)shell_clusters + 1u; /* root cluster + FLINT.DAT */
    if (min_cc < FAT32_MIN_USABLE_CLUSTERS)
        min_cc = FAT32_MIN_USABLE_CLUSTERS;

    const uint16_t rsv = 32u;
    const uint8_t nfats = 2u;
    uint32_t fat_sec = 0u, cc = 0u;
    uint32_t tot_sec = rsv + nfats * 64u + min_cc * (uint32_t)spc; /* initial guess */
    for (int grow = 0; grow < 5000000; grow++) {
        if (fat32_iterate_layout(tot_sec, bps, spc, rsv, nfats, &fat_sec, &cc) != 0) {
            tot_sec += (uint32_t)spc * 64u;
            continue;
        }
        if (cc >= min_cc)
            break;
        tot_sec += (uint32_t)spc;
    }
    if (cc < min_cc)
        return -1;

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    if (ftruncate(fd, (off_t)((uint64_t)tot_sec * (uint64_t)bps)) != 0) {
        close(fd);
        return -1;
    }

    uint8_t *z = calloc(1, (size_t)bps);
    if (!z) {
        close(fd);
        return -1;
    }
    for (uint32_t s = 0; s < tot_sec; s++) {
        if (pwrite(fd, z, bps, (off_t)((uint64_t)s * (uint64_t)bps)) != (ssize_t)bps) {
            free(z);
            close(fd);
            return -1;
        }
    }
    free(z);

    uint8_t bs[512];
    memset(bs, 0, sizeof(bs));
    bs[0] = 0xEB;
    bs[1] = 0x58;
    bs[2] = 0x90;
    memcpy(bs + 3, "MSWIN4.1", 8);
    st_le16(bs + 0x0B, bps);
    bs[0x0D] = spc;
    st_le16(bs + 0x0E, rsv);
    bs[0x10] = nfats;
    st_le16(bs + 0x11, 0);
    st_le16(bs + 0x13, 0);
    bs[0x15] = 0xF8;
    st_le16(bs + 0x18, 0);
    st_le32(bs + 0x20, tot_sec);
    st_le32(bs + 0x24, fat_sec);
    st_le32(bs + 0x2C, 2u); /* root cluster */
    st_le16(bs + 0x30, 1u); /* FSInfo sector */
    st_le16(bs + 0x32, 6u); /* backup boot */
    bs[0x40] = 0x80;
    bs[0x42] = 0x29;
    st_le32(bs + 0x43, 0xC0FFEEu); /* serial */
    fat32_set_label(bs + 0x47, volume_label ? volume_label : "NO NAME");
    memcpy(bs + 0x52, "FAT32   ", 8);
    st_le16(bs + 0x1FE, 0xAA55);

    if (pwrite(fd, bs, sizeof(bs), 0) != (ssize_t)sizeof(bs)) {
        close(fd);
        return -1;
    }
    /* Backup boot at sector 6 */
    if (pwrite(fd, bs, sizeof(bs), (off_t)(6u * (uint64_t)bps)) != (ssize_t)sizeof(bs)) {
        close(fd);
        return -1;
    }

    /* FSInfo sector (sector 1): minimal valid values */
    uint8_t fsinfo[512];
    memset(fsinfo, 0, sizeof(fsinfo));
    st_le32(fsinfo, 0x41615252);
    st_le32(fsinfo + 0x1E4, 0x61417272);
    st_le32(fsinfo + 0x1E8, 0xFFFFFFFFu); /* free count unknown */
    st_le32(fsinfo + 0x1EC, 0xFFFFFFFFu); /* next free unknown */
    st_le16(fsinfo + 0x1FE, 0xAA55u);
    if (pwrite(fd, fsinfo, sizeof(fsinfo), (off_t)bps) != (ssize_t)sizeof(fsinfo)) {
        close(fd);
        return -1;
    }

    uint32_t fat_bytes = fat_sec * (uint32_t)bps;
    uint8_t *fat = calloc(1, fat_bytes);
    if (!fat) {
        close(fd);
        return -1;
    }
    st_le32(fat + 0, 0x0FFFFFF8u); /* media / reserved */
    st_le32(fat + 4, 0xFFFFFFFFu);
    st_le32(fat + 8, 0x0FFFFFF8u); /* root cluster EOF */
    uint32_t last_cl = 3u + (uint32_t)shell_clusters - 1u;
    for (uint32_t c = 3u; c <= last_cl; c++) {
        uint32_t off = c * 4u;
        if (c == last_cl)
            st_le32(fat + off, 0x0FFFFFF8u);
        else
            st_le32(fat + off, c + 1u);
    }
    uint64_t fat0_off = (uint64_t)rsv * (uint64_t)bps;
    if (pwrite(fd, fat, fat_bytes, (off_t)fat0_off) != (ssize_t)fat_bytes) {
        free(fat);
        close(fd);
        return -1;
    }
    uint64_t fat1_off = fat0_off + (uint64_t)fat_sec * (uint64_t)bps;
    if (pwrite(fd, fat, fat_bytes, (off_t)fat1_off) != (ssize_t)fat_bytes) {
        free(fat);
        close(fd);
        return -1;
    }
    free(fat);

    uint32_t first_data_sec = (uint32_t)rsv + (uint32_t)nfats * fat_sec;
    uint64_t root_off = (uint64_t)first_data_sec * (uint64_t)bps +
                        (uint64_t)(2u - 2u) * (uint64_t)spc * (uint64_t)bps;
    uint8_t *rootbuf = calloc(1, (size_t)spc * (size_t)bps);
    if (!rootbuf) {
        close(fd);
        return -1;
    }
    /* Volume label entry */
    fat32_set_label(rootbuf, volume_label ? volume_label : "NO NAME");
    rootbuf[0x0B] = 0x08;
    /* FLINT.DAT */
    uint8_t *de = rootbuf + 32;
    memcpy(de, "FLINT   DAT", 11);
    de[0x0B] = 0x20;
    st_le16(de + 0x14, 0); /* cluster high */
    st_le16(de + 0x1A, 3);  /* cluster low start at 3 */
    st_le32(de + 0x1C, (uint32_t)shell_clusters * (uint32_t)bytes_per_cluster);

    if (pwrite(fd, rootbuf, (size_t)spc * (size_t)bps, (off_t)root_off) != (ssize_t)((size_t)spc * (size_t)bps)) {
        free(rootbuf);
        close(fd);
        return -1;
    }
    free(rootbuf);

    /* Initialize FLINT.DAT clusters (first byte chain + volume name like legacy format) */
    uint32_t bpc = (uint32_t)bytes_per_cluster;
    for (int i = 0; i < shell_clusters; i++) {
        uint8_t *buf = calloc(1, bpc);
        if (!buf) {
            close(fd);
            return -1;
        }
        for (uint32_t j = 0; j < bpc; j++)
            buf[j] = (uint8_t)(rand() % 256);
        if (i == 0)
            buf[0] = (uint8_t)((shell_clusters > 1) ? 1u : 0u);
        else if (i == shell_clusters - 1)
            buf[0] = 0;
        else
            buf[0] = (uint8_t)(i + 1);
        int vlen = (int)strlen(flint_volume_name ? flint_volume_name : "");
        int copy = vlen < (int)bpc - 1 ? vlen : (int)bpc - 1;
        if (copy > 0)
            asm_mem_copy(buf + 1, flint_volume_name, (size_t)copy);
        uint32_t cl = 3u + (uint32_t)i;
        uint64_t byte_off = (uint64_t)first_data_sec * (uint64_t)bps +
                            (uint64_t)(cl - 2u) * (uint64_t)spc * (uint64_t)bps;
        if (pwrite(fd, buf, bpc, (off_t)byte_off) != (ssize_t)bpc) {
            free(buf);
            close(fd);
            return -1;
        }
        free(buf);
    }

    close(fd);
    return 0;
}
