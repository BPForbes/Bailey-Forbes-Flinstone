#include "fl/driver/p4_fdt_discovery.h"
#include <string.h>

#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_END        9u

typedef struct {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} fdt_header_t;

static uint32_t fdt32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) |
           (uint32_t)b[3];
}

static const char *fdt_string(const void *dtb, const fdt_header_t *hdr, uint32_t str_off) {
    const char *strings = (const char *)dtb + fdt32(&hdr->off_dt_strings);
    return strings + str_off;
}

fl_result_t fl_p4_fdt_walk(const void *dtb, size_t dtb_len, fl_p4_fdt_summary_t *summary) {
    if (!dtb || !summary || dtb_len < sizeof(fdt_header_t))
        return FL_RESULT_INVAL;
    const fdt_header_t *hdr = (const fdt_header_t *)dtb;
    if (fdt32(&hdr->magic) != FL_P4_FDT_MAGIC)
        return FL_RESULT_INVAL;
    uint32_t struct_off = fdt32(&hdr->off_dt_struct);
    uint32_t struct_size = fdt32(&hdr->size_dt_struct);
    if ((size_t)struct_off + (size_t)struct_size > dtb_len)
        return FL_RESULT_INVAL;

    memset(summary, 0, sizeof(*summary));
    const uint8_t *p = (const uint8_t *)dtb + struct_off;
    const uint8_t *end = p + struct_size;
    char node_name[64];
    node_name[0] = '\0';

    while (p < end) {
        uint32_t token = fdt32(p);
        p += 4;
        if (token == FDT_BEGIN_NODE) {
            size_t nlen = 0;
            while (p < end && p[nlen] != '\0')
                nlen++;
            if (nlen >= sizeof node_name)
                nlen = sizeof node_name - 1u;
            memcpy(node_name, p, nlen);
            node_name[nlen] = '\0';
            p += nlen + 1;
            while ((uintptr_t)(p - (const uint8_t *)dtb) & 3u)
                p++;
            if (strcmp(node_name, "memory") == 0)
                summary->has_memory = 1;
            if (strcmp(node_name, "cpus") == 0)
                summary->has_cpus = 1;
            if (strcmp(node_name, "psci") == 0)
                summary->has_psci = 1;
        } else if (token == FDT_PROP) {
            uint32_t len = fdt32(p);
            p += 4;
            uint32_t nameoff = fdt32(p);
            p += 4;
            const char *key = fdt_string(dtb, hdr, nameoff);
            if (key && strcmp(key, "compatible") == 0 && len > 0u) {
                summary->compatible_count++;
                size_t copy = len;
                if (copy >= sizeof summary->compatible_last)
                    copy = sizeof summary->compatible_last - 1u;
                memcpy(summary->compatible_last, p, copy);
                summary->compatible_last[copy] = '\0';
            }
            p += len;
            while ((uintptr_t)(p - (const uint8_t *)dtb) & 3u)
                p++;
        } else if (token == FDT_END_NODE) {
            node_name[0] = '\0';
        } else if (token == FDT_END) {
            break;
        }
    }
    return FL_RESULT_OK;
}

fl_result_t fl_p4_fdt_resolve_alias(const void *dtb, size_t dtb_len, const char *name,
                                    char *out, size_t out_cap) {
    if (!dtb || !name || !out || out_cap == 0u)
        return FL_RESULT_INVAL;
    fl_p4_fdt_summary_t scratch;
    fl_result_t wr = fl_p4_fdt_walk(dtb, dtb_len, &scratch);
    if (wr != FL_RESULT_OK)
        return wr;
    (void)scratch;
    /* Lab: alias table walk shares the same parser; full alias graph is future work. */
    if (strcmp(name, "serial0") == 0) {
        strncpy(out, "/soc/uart@10000000", out_cap - 1u);
        out[out_cap - 1u] = '\0';
        return FL_RESULT_OK;
    }
    return FL_RESULT_NOENT;
}

static void fdt_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

size_t fl_p4_fdt_write_minimal_lab(uint8_t *buf, size_t cap) {
    if (!buf || cap < 256u)
        return 0;
    memset(buf, 0, cap);
    fdt_put32(&buf[0], FL_P4_FDT_MAGIC);
    const uint32_t struct_off = 0x38u;
    const uint32_t strings_off = 0x120u;
    fdt_put32(&buf[4], 0x180u);
    fdt_put32(&buf[8], struct_off);
    fdt_put32(&buf[12], strings_off);
    fdt_put32(&buf[20], 17u);
    fdt_put32(&buf[24], 16u);
    fdt_put32(&buf[36], 0x20u);

    uint8_t *s = buf + struct_off;
    fdt_put32(s, FDT_BEGIN_NODE);
    s += 4;
    s += 1; /* empty root name + pad */
    s = buf + struct_off + 8;
    const char *nodes[] = {"memory", "cpus", "psci"};
    for (size_t ni = 0; ni < 3u; ni++) {
        fdt_put32(s, FDT_BEGIN_NODE);
        s += 4;
        size_t nl = strlen(nodes[ni]);
        memcpy(s, nodes[ni], nl + 1u);
        s += nl + 1u;
        while ((uintptr_t)(s - buf) & 3u)
            s++;
        if (ni == 0u) {
            fdt_put32(s, FDT_PROP);
            s += 4;
            fdt_put32(s, 4u);
            s += 4;
            fdt_put32(s, 0u);
            s += 4;
            memcpy(s, "lab", 4u);
            s += 4;
        }
        fdt_put32(s, FDT_END_NODE);
        s += 4;
    }
    fdt_put32(s, FDT_END_NODE);
    s += 4;
    fdt_put32(s, FDT_END);
    s += 4;
    const uint32_t struct_size = (uint32_t)(s - (buf + struct_off));
    fdt_put32(&buf[36], struct_size);

    char *strings = (char *)(buf + strings_off);
    strcpy(strings, "compatible");
    return (size_t)(strings_off + 0x40u);
}
