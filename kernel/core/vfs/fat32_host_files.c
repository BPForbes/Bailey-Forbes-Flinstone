/*
 * FAT32 host image: store arbitrary binary files in the root directory (8.3 / mangled names).
 */

#include "fat32_host.h"
#include "common.h"
#include "disk_host_asm.h"
#include "mem_asm.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t ld_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ld_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void st_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void st_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static uint64_t fat_byte_off(const Fat32HostVol *v, uint32_t cluster_number) {
    return (uint64_t)v->reserved_sectors * (uint64_t)v->bytes_per_sector +
           (uint64_t)cluster_number * 4u;
}

static uint64_t fat1_byte_off(const Fat32HostVol *v) {
    return (uint64_t)v->reserved_sectors * (uint64_t)v->bytes_per_sector +
           (uint64_t)v->sectors_per_fat32 * (uint64_t)v->bytes_per_sector;
}

static int cluster_data_byte_off(const Fat32HostVol *v, uint32_t cl, uint64_t *out) {
    if (cl < 2u)
        return -1;
    uint32_t bps = v->bytes_per_sector;
    uint32_t spc = v->sectors_per_cluster;
    *out = (uint64_t)v->first_data_sector * (uint64_t)bps +
           (uint64_t)(cl - 2u) * (uint64_t)spc * (uint64_t)bps;
    return 0;
}

static uint32_t max_cluster_number(const Fat32HostVol *v) {
    return v->data_cluster_count + 1u;
}

static uint32_t flint_last_cluster(const Fat32HostVol *v) {
    return v->flint_first_cluster + (uint32_t)v->shell_clusters - 1u;
}

static uint32_t fat_read_entry(int fd, const Fat32HostVol *v, uint32_t cl) {
    uint8_t b[4];
    uint64_t off = fat_byte_off(v, cl);
    if (disk_host_pread_vol(fd, b, 4, (off_t)off) != 4)
        return 0xFFFFFFFFu;
    return ld_le32(b) & 0x0FFFFFFFu;
}

static int fat_write_entry(int fd, const Fat32HostVol *v, uint32_t cl, uint32_t val28) {
    uint8_t b[4];
    st_le32(b, val28 & 0x0FFFFFFFu);
    uint64_t o0 = fat_byte_off(v, cl);
    uint64_t o1 = fat1_byte_off(v) + (uint64_t)cl * 4u;
    if (disk_host_pwrite_vol(fd, b, 4, (off_t)o0) != 4)
        return -1;
    if (disk_host_pwrite_vol(fd, b, 4, (off_t)o1) != 4)
        return -1;
    return 0;
}

static int root_dir_byte_off(const Fat32HostVol *v, uint64_t *out) {
    return cluster_data_byte_off(v, v->root_cluster, out);
}

static int is_dos_char(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    if (c == '_' || c == ' ' || c == '$' || c == '%' || c == '\'' || c == '-' || c == '{' || c == '}' ||
        c == '~' || c == '!' || c == '(' || c == ')' || c == '@' || c == '^')
        return 1;
    return 0;
}

static void ascii_upcase_inplace(char *s) {
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
    }
}

static int pack_83(const char *base, const char *ext, char out11[11]) {
    memset(out11, ' ', 11);
    if (!base || !base[0])
        return -1;
    char b[9] = {0}, e[4] = {0};
    strncpy(b, base, 8);
    if (ext && ext[0])
        strncpy(e, ext, 3);
    ascii_upcase_inplace(b);
    ascii_upcase_inplace(e);
    for (char *p = b; *p; p++) {
        if (!is_dos_char((unsigned char)*p) && *p != '.')
            *p = '_';
    }
    for (char *p = e; *p; p++) {
        if (!is_dos_char((unsigned char)*p))
            *p = '_';
    }
    size_t bl = strlen(b);
    if (bl > 8u)
        return -1;
    memcpy(out11, b, bl);
    size_t el = strlen(e);
    if (el > 3u)
        return -1;
    memcpy(out11 + 8, e, el);
    return 0;
}

static void split_base_ext(const char *path, char *base, size_t base_sz, char *ext, size_t ext_sz) {
    const char *s = strrchr(path, '/');
    s = s ? s + 1 : path;
    const char *dot = strrchr(s, '.');
    if (dot && dot != s) {
        size_t bl = (size_t)(dot - s);
        if (bl >= base_sz)
            bl = base_sz - 1;
        memcpy(base, s, bl);
        base[bl] = '\0';
        strncpy(ext, dot + 1, ext_sz - 1);
        ext[ext_sz - 1] = '\0';
    } else {
        strncpy(base, s, base_sz - 1);
        base[base_sz - 1] = '\0';
        ext[0] = '\0';
    }
}

static int name_fits_short(const char *base, const char *ext) {
    return strlen(base) <= 8u && strlen(ext) <= 3u;
}

static int build_long_mangle(const char *base, const char *ext, int tilde_idx, char out11[11]) {
    char b7[7] = {0};
    size_t n = strlen(base);
    size_t take = n > 6u ? 6u : n;
    memcpy(b7, base, take);
    ascii_upcase_inplace(b7);
    for (char *p = b7; *p; p++) {
        if (!is_dos_char((unsigned char)*p))
            *p = '_';
    }
    char e3[4] = {0};
    strncpy(e3, ext, 3);
    ascii_upcase_inplace(e3);
    for (char *p = e3; *p; p++) {
        if (!is_dos_char((unsigned char)*p))
            *p = '_';
    }
    memset(out11, ' ', 11);
    memcpy(out11, b7, strlen(b7) > 6u ? 6u : strlen(b7));
    out11[6] = '~';
    if (tilde_idx < 1)
        tilde_idx = 1;
    if (tilde_idx > 9)
        return -1;
    out11[7] = (char)('0' + tilde_idx);
    size_t el = strlen(e3);
    if (el > 3u)
        el = 3u;
    memcpy(out11 + 8, e3, el);
    return 0;
}

static int dirent_matches_83(const uint8_t *e, const char name11[11]) {
    return memcmp(e, name11, 11) == 0;
}

/* Returns: 1 = found name (slot set), 0 = not found but slot is first free (slot set), -2 = root full, -1 = I/O */
static int find_dirent_root(int fd, const Fat32HostVol *v, const char name11[11], int *slot_out,
                            uint8_t *ent_copy32) {
    uint64_t root_off;
    if (root_dir_byte_off(v, &root_off) != 0)
        return -1;
    uint32_t bpc = (uint32_t)v->bytes_per_cluster * (uint32_t)v->sectors_per_cluster;
    uint8_t *root = malloc(bpc);
    if (!root)
        return -1;
    if (disk_host_pread_vol(fd, root, bpc, (off_t)root_off) != (ssize_t)bpc) {
        free(root);
        return -1;
    }
    int nent = (int)(bpc / 32u);
    int match = -1;
    int empty = -1;
    for (int i = 0; i < nent; i++) {
        uint8_t *e = root + (uint32_t)i * 32u;
        if (e[0] == 0) {
            if (empty < 0)
                empty = i;
            continue;
        }
        if (e[0] == 0xE5) {
            if (empty < 0)
                empty = i;
            continue;
        }
        if ((e[0x0B] & 0x08) != 0)
            continue;
        if (dirent_matches_83(e, name11)) {
            match = i;
            if (ent_copy32)
                memcpy(ent_copy32, e, 32);
            break;
        }
    }
    if (match >= 0) {
        *slot_out = match;
        free(root);
        return 1;
    }
    if (empty >= 0) {
        *slot_out = empty;
        free(root);
        return 0;
    }
    free(root);
    return -2;
}

static int write_dirent_slot(int fd, const Fat32HostVol *v, int slot, const uint8_t ent[32]) {
    uint64_t root_off;
    if (root_dir_byte_off(v, &root_off) != 0)
        return -1;
    uint64_t ent_off = root_off + (uint64_t)slot * 32u;
    return disk_host_pwrite_vol(fd, ent, 32, (off_t)ent_off) == 32 ? 0 : -1;
}

static void free_cluster_chain(int fd, const Fat32HostVol *v, uint32_t first) {
    if (first < 2u)
        return;
    uint32_t c = first;
    for (;;) {
        uint32_t next = fat_read_entry(fd, v, c);
        fat_write_entry(fd, v, c, 0u);
        if (next >= 0x0FFFFFF8u)
            break;
        c = next;
        if (c < 2u)
            break;
    }
}

static int collect_free_clusters(int fd, const Fat32HostVol *v, uint32_t need, uint32_t *chain) {
    if (need == 0u)
        return 0;
    uint32_t start = flint_last_cluster(v) + 1u;
    uint32_t maxc = max_cluster_number(v);
    uint32_t got = 0;
    for (uint32_t c = start; c <= maxc && got < need; c++) {
        if (fat_read_entry(fd, v, c) == 0u)
            chain[got++] = c;
    }
    return got == need ? 0 : -1;
}

static int allocate_and_write_chain(int fd, const Fat32HostVol *v, int src_fd, size_t file_sz,
                                    uint32_t *first_clu_out) {
    uint32_t bpc = (uint32_t)v->bytes_per_cluster * (uint32_t)v->sectors_per_cluster;
    if (file_sz == 0u) {
        *first_clu_out = 0u;
        return 0;
    }
    uint32_t need = (uint32_t)((file_sz + (uint64_t)bpc - 1u) / (uint64_t)bpc);
    if (need == 0u)
        need = 1u;
    uint32_t *ch = (uint32_t *)malloc((size_t)need * sizeof(uint32_t));
    if (!ch)
        return -1;
    if (collect_free_clusters(fd, v, need, ch) != 0) {
        free(ch);
        return -1;
    }
    for (uint32_t i = 0; i + 1u < need; i++) {
        if (fat_write_entry(fd, v, ch[i], ch[i + 1u]) != 0) {
            for (uint32_t j = 0; j < i; j++)
                fat_write_entry(fd, v, ch[j], 0u);
            free(ch);
            return -1;
        }
    }
    if (fat_write_entry(fd, v, ch[need - 1u], 0x0FFFFFF8u) != 0) {
        for (uint32_t j = 0; j + 1u < need; j++)
            fat_write_entry(fd, v, ch[j], 0u);
        fat_write_entry(fd, v, ch[need - 1u], 0u);
        free(ch);
        return -1;
    }

    unsigned char *buf = (unsigned char *)malloc(bpc);
    if (!buf) {
        for (uint32_t j = 0; j < need; j++)
            fat_write_entry(fd, v, ch[j], 0u);
        free(ch);
        return -1;
    }
    size_t left = file_sz;
    off_t rd_off = 0;
    for (uint32_t i = 0; i < need; i++) {
        asm_mem_zero(buf, bpc);
        size_t chunk = left > (size_t)bpc ? (size_t)bpc : left;
        if (chunk > 0u) {
            ssize_t rn = disk_host_pread_vol(src_fd, buf, chunk, rd_off);
            if (rn != (ssize_t)chunk) {
                free(buf);
                for (uint32_t j = 0; j < need; j++)
                    fat_write_entry(fd, v, ch[j], 0u);
                free(ch);
                return -1;
            }
            rd_off += (off_t)chunk;
            left -= chunk;
        }
        uint64_t d_off;
        if (cluster_data_byte_off(v, ch[i], &d_off) != 0) {
            free(buf);
            for (uint32_t j = 0; j < need; j++)
                fat_write_entry(fd, v, ch[j], 0u);
            free(ch);
            return -1;
        }
        if (disk_host_pwrite_vol(fd, buf, bpc, (off_t)d_off) != (ssize_t)bpc) {
            free(buf);
            for (uint32_t j = 0; j < need; j++)
                fat_write_entry(fd, v, ch[j], 0u);
            free(ch);
            return -1;
        }
    }
    free(buf);
    *first_clu_out = ch[0];
    free(ch);
    return 0;
}

static int read_chain_to_host(int img_fd, const Fat32HostVol *v, uint32_t first, size_t fsz, int out_fd) {
    if (fsz == 0u)
        return 0;
    uint32_t bpc = (uint32_t)v->bytes_per_cluster * (uint32_t)v->sectors_per_cluster;
    uint32_t c = first;
    size_t left = fsz;
    off_t dst_off = 0;
    unsigned char *buf = (unsigned char *)malloc(bpc);
    if (!buf)
        return -1;
    while (left > 0u) {
        uint64_t d_off;
        if (cluster_data_byte_off(v, c, &d_off) != 0) {
            free(buf);
            return -1;
        }
        if (disk_host_pread_vol(img_fd, buf, bpc, (off_t)d_off) != (ssize_t)bpc) {
            free(buf);
            return -1;
        }
        size_t chunk = left > (size_t)bpc ? (size_t)bpc : left;
        if (disk_host_pwrite_vol(out_fd, buf, chunk, dst_off) != (ssize_t)chunk) {
            free(buf);
            return -1;
        }
        dst_off += (off_t)chunk;
        left -= chunk;
        if (left == 0u)
            break;
        uint32_t nx = fat_read_entry(img_fd, v, c);
        if (nx >= 0x0FFFFFF8u) {
            free(buf);
            return -1;
        }
        c = nx;
        if (c < 2u) {
            free(buf);
            return -1;
        }
    }
    free(buf);
    return 0;
}

static int human_name_to_11(const char *in, char out11[11]) {
    memset(out11, ' ', 11);
    const char *dot = strrchr(in, '.');
    if (dot && dot != in) {
        size_t bl = (size_t)(dot - in);
        if (bl > 8u)
            return -1;
        memcpy(out11, in, bl);
        const char *ext = dot + 1;
        size_t el = strlen(ext);
        if (el > 3u)
            return -1;
        memcpy(out11 + 8, ext, el);
    } else {
        size_t n = strlen(in);
        if (n > 11u)
            return -1;
        memcpy(out11, in, n);
    }
    for (int i = 0; i < 11; i++) {
        if (out11[i] >= 'a' && out11[i] <= 'z')
            out11[i] = (char)(out11[i] - 32);
    }
    return 0;
}

static void format_83_display(const char n11[11], char *dst, size_t dstsz) {
    char base[9] = {0};
    char ext[4] = {0};
    memcpy(base, n11, 8);
    memcpy(ext, n11 + 8, 3);
    while (strlen(base) > 0 && base[strlen(base) - 1] == ' ')
        base[strlen(base) - 1] = '\0';
    while (strlen(ext) > 0 && ext[strlen(ext) - 1] == ' ')
        ext[strlen(ext) - 1] = '\0';
    if (ext[0])
        snprintf(dst, dstsz, "%s.%s", base, ext);
    else
        snprintf(dst, dstsz, "%s", base);
}

static int pick_mangled_name(int fd, const Fat32HostVol *v, const char *base, const char *ext,
                              char out11[11]) {
    if (name_fits_short(base, ext)) {
        if (pack_83(base, ext, out11) != 0)
            return -1;
        int slot;
        int r = find_dirent_root(fd, v, out11, &slot, NULL);
        if (r == 0)
            return 0;
        if (r < 0)
            return -1;
    }
    for (int t = 1; t <= 9; t++) {
        if (build_long_mangle(base, ext, t, out11) != 0)
            continue;
        int slot;
        int r = find_dirent_root(fd, v, out11, &slot, NULL);
        if (r == 1)
            continue;
        if (r == -2)
            return -1;
        if (r < 0)
            return -1;
        if (r == 0)
            return 0;
    }
    return -1;
}

int fat32_host_file_put(const char *host_src_path, const char *name_on_disk_or_null) {
    if (!g_disk_host_fat32 || !g_fat32_host_vol.valid || !host_src_path)
        return -1;
    struct stat st;
    if (stat(host_src_path, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    size_t file_sz = (size_t)st.st_size;

    int img = open(current_disk_file, O_RDWR);
    if (img < 0)
        return -1;
    int src = open(host_src_path, O_RDONLY);
    if (src < 0) {
        close(img);
        return -1;
    }

    char name11[11];
    memset(name11, ' ', sizeof(name11));
    if (name_on_disk_or_null && name_on_disk_or_null[0]) {
        if (human_name_to_11(name_on_disk_or_null, name11) != 0) {
            char base[64], ext[16];
            split_base_ext(name_on_disk_or_null, base, sizeof(base), ext, sizeof(ext));
            ascii_upcase_inplace(base);
            ascii_upcase_inplace(ext);
            if (pick_mangled_name(img, &g_fat32_host_vol, base, ext, name11) != 0) {
                close(src);
                close(img);
                return -1;
            }
        }
    } else {
        char base[64], ext[16];
        split_base_ext(host_src_path, base, sizeof(base), ext, sizeof(ext));
        ascii_upcase_inplace(base);
        ascii_upcase_inplace(ext);
        if (pick_mangled_name(img, &g_fat32_host_vol, base, ext, name11) != 0) {
            close(src);
            close(img);
            return -1;
        }
    }

    if (memcmp(name11, "FLINT   DAT", 11) == 0) {
        close(src);
        close(img);
        return -1;
    }

    int slot;
    uint8_t oldent[32];
    int fr = find_dirent_root(img, &g_fat32_host_vol, name11, &slot, oldent);
    if (fr < 0) {
        close(src);
        close(img);
        return -1;
    }
    if (fr == 1) {
        if (memcmp(oldent, "FLINT   DAT", 11) == 0) {
            close(src);
            close(img);
            return -1;
        }
        uint32_t oldc = ld_le16(oldent + 0x1A) | ((uint32_t)ld_le16(oldent + 0x14) << 16);
        free_cluster_chain(img, &g_fat32_host_vol, oldc);
    } else if (fr == -2) {
        close(src);
        close(img);
        return -1;
    }

    uint32_t first_clu = 0;
    if (allocate_and_write_chain(img, &g_fat32_host_vol, src, file_sz, &first_clu) != 0) {
        close(src);
        close(img);
        return -1;
    }
    close(src);

    uint8_t de[32];
    memset(de, 0, sizeof(de));
    memcpy(de, name11, 11);
    de[0x0B] = 0x20;
    st_le16(de + 0x14, (uint16_t)((first_clu >> 16) & 0xFFFFu));
    st_le16(de + 0x1A, (uint16_t)(first_clu & 0xFFFFu));
    st_le32(de + 0x1C, (uint32_t)file_sz);

    if (write_dirent_slot(img, &g_fat32_host_vol, slot, de) != 0) {
        free_cluster_chain(img, &g_fat32_host_vol, first_clu);
        close(img);
        return -1;
    }
    fsync(img);
    close(img);
    return 0;
}

int fat32_host_file_get(const char *name_on_disk_83, const char *host_dst_path) {
    if (!g_disk_host_fat32 || !g_fat32_host_vol.valid || !name_on_disk_83 || !host_dst_path)
        return -1;
    char name11[11];
    if (human_name_to_11(name_on_disk_83, name11) != 0)
        return -1;
    if (memcmp(name11, "FLINT   DAT", 11) == 0)
        return -1;
    int img = open(current_disk_file, O_RDONLY);
    if (img < 0)
        return -1;
    int slot;
    uint8_t ent[32];
    int fr = find_dirent_root(img, &g_fat32_host_vol, name11, &slot, ent);
    if (fr != 1) {
        close(img);
        return -1;
    }
    if (memcmp(ent, "FLINT   DAT", 11) == 0) {
        close(img);
        return -1;
    }
    uint32_t first = ld_le16(ent + 0x1A) | ((uint32_t)ld_le16(ent + 0x14) << 16);
    uint32_t fsz32 = ld_le32(ent + 0x1C);
    size_t fsz = (size_t)fsz32;
    int out = open(host_dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(img);
        return -1;
    }
    int rr = read_chain_to_host(img, &g_fat32_host_vol, first, fsz, out);
    fsync(out);
    close(out);
    close(img);
    return rr;
}

int fat32_host_file_del(const char *name_on_disk_83) {
    if (!g_disk_host_fat32 || !g_fat32_host_vol.valid || !name_on_disk_83)
        return -1;
    char name11[11];
    if (human_name_to_11(name_on_disk_83, name11) != 0)
        return -1;
    if (memcmp(name11, "FLINT   DAT", 11) == 0)
        return -1;
    int img = open(current_disk_file, O_RDWR);
    if (img < 0)
        return -1;
    int slot;
    uint8_t ent[32];
    int fr = find_dirent_root(img, &g_fat32_host_vol, name11, &slot, ent);
    if (fr != 1) {
        close(img);
        return -1;
    }
    uint32_t first = ld_le16(ent + 0x1A) | ((uint32_t)ld_le16(ent + 0x14) << 16);
    free_cluster_chain(img, &g_fat32_host_vol, first);
    ent[0] = 0xE5;
    if (write_dirent_slot(img, &g_fat32_host_vol, slot, ent) != 0) {
        close(img);
        return -1;
    }
    fsync(img);
    close(img);
    return 0;
}

void fat32_host_file_list(void) {
    if (!g_disk_host_fat32 || !g_fat32_host_vol.valid) {
        printf("diskfiles: requires a loaded FAT32 disk image (setdisk / drive.img).\n");
        return;
    }
    int fd = open(current_disk_file, O_RDONLY);
    if (fd < 0) {
        perror("open disk");
        return;
    }
    uint64_t root_off;
    if (root_dir_byte_off(&g_fat32_host_vol, &root_off) != 0) {
        close(fd);
        return;
    }
    uint32_t bpc = (uint32_t)g_fat32_host_vol.bytes_per_cluster *
                   (uint32_t)g_fat32_host_vol.sectors_per_cluster;
    uint8_t *root = malloc(bpc);
    if (!root) {
        close(fd);
        return;
    }
    if (disk_host_pread_vol(fd, root, bpc, (off_t)root_off) != (ssize_t)bpc) {
        free(root);
        close(fd);
        return;
    }
    int nent = (int)(bpc / 32u);
    printf("Root files on %s:\n", current_disk_file);
    for (int i = 0; i < nent; i++) {
        uint8_t *e = root + (uint32_t)i * 32u;
        if (e[0] == 0 || e[0] == 0xE5)
            continue;
        if ((e[0x0B] & 0x08) != 0)
            continue;
        if ((e[0x0B] & 0x10) != 0)
            continue; /* skip directories */
        char disp[32];
        format_83_display((const char *)e, disp, sizeof(disp));
        uint32_t sz = ld_le32(e + 0x1C);
        printf("  %-20s  %10u bytes\n", disp, (unsigned)sz);
    }
    free(root);
    close(fd);
}
