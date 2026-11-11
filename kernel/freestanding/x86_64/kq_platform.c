/*
 * What the KeyQuorum library and the existing KeyQuorum driver need from the freestanding image:
 * the four embedder hooks of keyquorum_identity.h, the mem_domain_* allocators the driver
 * calls, a UTC clock, and the handful of libc symbols the compiler and the Rust archive expect when
 * linking with -nostdlib. The same symbols tools/browser-lab/wasm/lab_kq.c supplies for the
 * WebAssembly lab; the byte primitives reuse the assembly layer (arch/x86_64/gas/mem_asm.s).
 *
 * Heap: one arena at a fixed physical range the boot code identity-maps (the first 1 GiB, 2 MiB
 * pages) and QEMU's 64 MiB provides, above the kernel image and its bss. A first-fit free list with
 * coalescing; every freed block is zeroed, as the library asks.
 *
 * Entropy: RDRAND, else the launcher's fw_cfg file opt/flintstone/entropy (below). With neither,
 * kq_platform_random returns non-zero and the library fails closed (a panic through
 * kq_platform_panic): no weak bytes, ever. QEMU's default CPU model has no RDRAND; `-cpu max` does.
 */
#include "contract_result.h"
#include "fl/mem_asm.h"
#include "ioport.h"
#include "mem_domain.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define KQ_HEAP_BASE 0x400000ull        /* 4 MiB: above the image (loaded at 64 KiB) and its bss */
#define KQ_HEAP_SIZE (40ull << 20)      /* 40 MiB: sealing an account key (Argon2id, 19 MiB) needs more than 19 MiB at once */
#define KQ_ALIGN 16u

typedef struct kq_blk {
    size_t size; /* payload bytes after this header */
    int used;
    struct kq_blk *next;
} kq_blk_t;

#define HDR ((size_t)((sizeof(kq_blk_t) + KQ_ALIGN - 1) & ~(size_t)(KQ_ALIGN - 1)))

static kq_blk_t *s_head;

static void heap_init(void)
{
    s_head = (kq_blk_t *)(uintptr_t)KQ_HEAP_BASE;
    s_head->size = KQ_HEAP_SIZE - HDR;
    s_head->used = 0;
    s_head->next = 0;
}

static void heap_split_tail(kq_blk_t *b, size_t need)
{
    kq_blk_t *t = (kq_blk_t *)((uintptr_t)b + HDR + need);

    t->size = b->size - need - HDR;
    t->used = 0;
    t->next = b->next;
    b->size = need;
    b->next = t;
}

/* Payload pointer aligned to `align` (<= 4096): the block's payload is over-sized so an aligned
 * address with a back-pointer to the block header fits inside it. */
static void *heap_alloc(size_t len, size_t align)
{
    kq_blk_t *b;
    size_t need;

    if (!s_head)
        heap_init();
    if (align < KQ_ALIGN)
        align = KQ_ALIGN;
    if (align > 4096 || (align & (align - 1)) || len > KQ_HEAP_SIZE)
        return 0;
    need = (len + align + sizeof(void *) + KQ_ALIGN - 1) & ~(size_t)(KQ_ALIGN - 1);
    for (b = s_head; b; b = b->next) {
        if (!b->used && b->size >= need) {
            uintptr_t raw = (uintptr_t)b + HDR + sizeof(void *);
            uintptr_t p = (raw + align - 1) & ~(uintptr_t)(align - 1);

            if (b->size >= need + HDR + KQ_ALIGN)
                heap_split_tail(b, need);
            b->used = 1;
            ((kq_blk_t **)p)[-1] = b;
            return (void *)p;
        }
    }
    return 0;
}

static void heap_free(void *ptr)
{
    kq_blk_t *b;

    if (!ptr)
        return;
    b = ((kq_blk_t **)ptr)[-1];
    asm_mem_secure_zero((void *)((uintptr_t)b + HDR), b->size); /* freed memory is zeroed */
    b->used = 0;
    for (b = s_head; b; b = b->next) /* coalesce neighbours */
        while (!b->used && b->next && !b->next->used) {
            b->size += HDR + b->next->size;
            b->next = b->next->next;
        }
}


uint8_t *kq_platform_alloc(size_t len, size_t align)
{
    return (uint8_t *)heap_alloc(len ? len : 1, align);
}

void kq_platform_free(uint8_t *ptr, size_t len, size_t align)
{
    (void)len;
    (void)align;
    heap_free(ptr);
}

__attribute__((noreturn)) void kq_platform_panic(void)
{
    /* Fail closed: the image has one address space and no process to end, so it stops. */
    fl_fs_serial_puts("FLINTSTONE_KQ_PANIC\r\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

static int have_rdrand(void)
{
    uint32_t a = 1, b, c = 0, d;

    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 30) & 1u;
}

#ifndef FL_SECFEAT
/* ---- fw_cfg entropy: the launcher's provider when the CPU has no RDRAND ------------------------
 * The launcher hands the image a file named opt/flintstone/entropy through QEMU's fw_cfg device (the driver is
 * kernel/drivers/freestanding_x86_64/fwcfg.c). It must hold fresh random bytes on every boot (the browser page:
 * crypto.getRandomValues; a shell: head -c 4096 /dev/urandom): the image cannot tell a reused file from a fresh
 * one, and a reused file would repeat keys. The policy is here: each byte is handed out once, never reused, and
 * running out fails closed like every other path. It is used only when RDRAND is absent. */
#include "fwcfg.h"

#define FW_ENTROPY_NAME "opt/flintstone/entropy"

static uint32_t s_fw_used;

static int fw_cfg_fill(uint8_t *buf, size_t len)
{
    if (len > 0xFFFFFFFFu - s_fw_used || fl_fs_fwcfg_read(FW_ENTROPY_NAME, s_fw_used, buf, len) != 0)
        return 1; /* no device, no file, or not enough left: fail closed */
    s_fw_used += (uint32_t)len;
    return 0;
}
#else
/* The SECFEAT image has about 512 bytes of room before the 0x9f000 limit: RDRAND only, as before. */
static int fw_cfg_fill(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return 1;
}
#endif

int kq_platform_random(uint8_t *buf, size_t len)
{
    if (!buf)
        return 1;
    if (!have_rdrand())
        return fw_cfg_fill(buf, len);
    while (len) {
        uint64_t v = 0;
        unsigned char ok = 0;
        size_t n = len < 8 ? len : 8;

        for (int tries = 0; tries < 10 && !ok; tries++)
            __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) : : "cc");
        if (!ok)
            return 1;
        for (size_t i = 0; i < n; i++)
            buf[i] = (uint8_t)(v >> (8 * i));
        buf += n;
        len -= n;
        v = 0;
    }
    return 0;
}


void *mem_domain_alloc(mem_domain_t d, size_t n)
{
    (void)d;
    return heap_alloc(n ? n : 1, KQ_ALIGN);
}

void *mem_domain_calloc(mem_domain_t d, size_t m, size_t n)
{
    void *p;

    if (n && m > (size_t)-1 / n)
        return 0;
    p = mem_domain_alloc(d, m * n);
    if (p)
        asm_mem_zero(p, m * n);
    return p;
}

void *mem_domain_realloc(mem_domain_t d, void *p, size_t n)
{
    void *q;
    kq_blk_t *b;
    size_t have;

    if (!p)
        return mem_domain_alloc(d, n);
    b = ((kq_blk_t **)p)[-1];
    have = b->size - ((uintptr_t)p - ((uintptr_t)b + HDR));
    if (n <= have)
        return p;
    q = mem_domain_alloc(d, n);
    if (!q)
        return 0;
    asm_mem_copy(q, p, have);
    heap_free(p);
    return q;
}

void mem_domain_free(mem_domain_t d, void *p)
{
    (void)d;
    heap_free(p);
}

void mem_domain_copy(void *dst, const void *src, size_t n)
{
    asm_mem_copy(dst, src, n);
}

void mem_domain_zero(void *p, size_t n)
{
    asm_mem_zero(p, n);
}

void mem_domain_secure_zero(void *p, size_t n)
{
    asm_mem_secure_zero(p, n);
}


static uint8_t cmos(uint8_t reg)
{
    fl_fs_outb(0x70, reg);
    return fl_fs_inb(0x71);
}

static unsigned bcd(uint8_t v, int is_bcd)
{
    return is_bcd ? (unsigned)((v & 0x0f) + (v >> 4) * 10u) : v;
}

/* An update in progress would tear the read: wait it out, then read twice. */
static void cmos_wait_stable_time(void)
{
    unsigned t1, t2;

    do {
        while (cmos(0x0a) & 0x80)
            ;
        t1 = cmos(0) | (cmos(2) << 8) | ((unsigned)cmos(4) << 16) | ((unsigned)cmos(7) << 24);
        t2 = cmos(0) | (cmos(2) << 8) | ((unsigned)cmos(4) << 16) | ((unsigned)cmos(7) << 24);
    } while (t1 != t2);
}

static unsigned hour_24(uint8_t hr, uint8_t status_b, int is_bcd)
{
    int pm = !(status_b & 2) && (hr & 0x80);
    unsigned h = bcd(hr & 0x7f, is_bcd);

    return pm ? (h % 12) + 12 : h;
}

static int64_t unix_days(unsigned y, unsigned mo, unsigned d)
{
    static const unsigned cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t days = (int64_t)(y - 1970) * 365 + (int64_t)((y - 1969) / 4) - (int64_t)((y - 1901) / 100) +
                   (int64_t)((y - 1601) / 400) + cum[mo - 1] + (d - 1);

    if (mo > 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        days++;
    return days;
}

fl_result_t fl_time_wall_sec(int64_t *out_sec)
{
    unsigned s, mi, h, d, mo, y;
    int is_bcd;
    uint8_t sb, hr;

    if (!out_sec)
        return FL_RESULT_INVAL;
    cmos_wait_stable_time();
    sb = cmos(0x0b);
    is_bcd = !(sb & 4);
    s = bcd(cmos(0), is_bcd);
    mi = bcd(cmos(2), is_bcd);
    hr = cmos(4);
    h = hour_24(hr, sb, is_bcd);
    d = bcd(cmos(7), is_bcd);
    mo = bcd(cmos(8), is_bcd);
    y = 2000 + bcd(cmos(9), is_bcd);
    if (mo < 1 || mo > 12 || d < 1 || d > 31)
        return FL_RESULT_ERR;
    *out_sec = unix_days(y, mo, d) * 86400 + (int64_t)h * 3600 + (int64_t)mi * 60 + s;
    return FL_RESULT_OK;
}

/* libc symbols the compiler and the Rust archive call under -nostdlib. Built with -fno-builtin
 * -fno-tree-loop-distribute-patterns so none of these turns into a call to itself. rep movsb /
 * stosb are fast enough here and need no SSE. */

void *memcpy(void *dst, const void *src, size_t n)
{
    void *r = dst;

    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return r;
}

void *memset(void *dst, int c, size_t n)
{
    void *r = dst;

    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return r;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n];
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;

    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;
    return n;
}
