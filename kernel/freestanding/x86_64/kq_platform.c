/* Freestanding KeyQuorum embedder: the four identity hooks, mem_domain_* allocators and libc symbols. */
/* Entropy is RDRAND, else fw_cfg; with neither, kq_platform_random fails closed. */
#include "contract_result.h"
#include "fl/mem_asm.h"
#include "ioport.h"
#include "kq_heap.h"
#include "mem_domain.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

uint8_t *kq_platform_alloc(size_t len, size_t align)
{
    return (uint8_t *)fl_kq_heap_alloc(len ? len : 1, align);
}

void kq_platform_free(uint8_t *ptr, size_t len, size_t align)
{
    (void)len;
    (void)align;
    fl_kq_heap_free(ptr);
}

__attribute__((noreturn)) void kq_platform_panic(void)
{
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
/* fw_cfg entropy, used only without RDRAND: each byte is handed out once; running out fails closed. */
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
/* The SECFEAT image: RDRAND only (fw_cfg is left out for want of room below 0x9f000). */
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
    return fl_kq_heap_alloc(n ? n : 1, FL_KQ_HEAP_ALIGN);
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
    size_t have;

    if (!p)
        return mem_domain_alloc(d, n);
    have = fl_kq_heap_usable(p);
    if (n <= have)
        return p;
    q = mem_domain_alloc(d, n);
    if (!q)
        return 0;
    asm_mem_copy(q, p, have);
    fl_kq_heap_free(p);
    return q;
}

void mem_domain_free(mem_domain_t d, void *p)
{
    (void)d;
    fl_kq_heap_free(p);
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

int mem_domain_equal(const void *a, const void *b, size_t n)
{
    return asm_mem_ct_equal(a, b, n);
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

/* libc symbols the compiler and Rust archive call under -nostdlib; built so none calls itself. */

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

/* The string and format calls the KeyQuorum checkpoints make (kq_checkpoint.c, fl/path.h). */

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n])
        n++;
    return n;
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;

    do {
        if (*s == (char)c)
            last = s;
    } while (*s++);
    return (char *)last;
}

/* Bases 0, 8, 10 and 16; no locale, no errno: an overflow saturates at ULONG_MAX. */
unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    unsigned long v = 0;
    int any = 0;

    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    if (*p == '+')
        p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2, base = 16;
    else if (base == 0)
        base = p[0] == '0' ? 8 : 10;
    for (;; p++) {
        unsigned d = *p >= '0' && *p <= '9'   ? (unsigned)(*p - '0')
                     : *p >= 'a' && *p <= 'z' ? (unsigned)(*p - 'a' + 10)
                     : *p >= 'A' && *p <= 'Z' ? (unsigned)(*p - 'A' + 10)
                                              : 99u;

        if (d >= (unsigned)base)
            break;
        v = v > (~0ul - d) / (unsigned)base ? ~0ul : v * (unsigned)base + d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return v;
}

typedef struct {
    char *out;
    size_t cap, n;
} fmt_t;

static void fmt_put(fmt_t *f, char c)
{
    if (f->n + 1 < f->cap)
        f->out[f->n] = c;
    f->n++;
}

static void fmt_pad(fmt_t *f, int count, char c)
{
    while (count-- > 0)
        fmt_put(f, c);
}

/* %s %c %d %i %u %x %X %%, flags '-' and '0', width and precision (or '*'), lengths l, ll, z. */
int vsnprintf(char *out, size_t cap, const char *fmt, __builtin_va_list ap)
{
    fmt_t f = {out, cap, 0};

    for (; *fmt; fmt++) {
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0, len;
        char digits[24];
        const char *str;

        if (*fmt != '%') {
            fmt_put(&f, *fmt);
            continue;
        }
        for (fmt++; *fmt == '-' || *fmt == '0'; fmt++)
            *fmt == '-' ? (left = 1) : (zero = 1);
        if (*fmt == '*')
            width = __builtin_va_arg(ap, int), fmt++;
        else
            for (; *fmt >= '0' && *fmt <= '9'; fmt++)
                width = width * 10 + (*fmt - '0');
        if (width < 0)
            left = 1, width = -width;
        if (*fmt == '.') {
            prec = 0;
            if (*++fmt == '*')
                prec = __builtin_va_arg(ap, int), fmt++;
            else
                for (; *fmt >= '0' && *fmt <= '9'; fmt++)
                    prec = prec * 10 + (*fmt - '0');
        }
        for (; *fmt == 'l' || *fmt == 'z'; fmt++)
            lng = 1;
        switch (*fmt) {
        case 's':
            str = __builtin_va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            len = (int)strnlen(str, prec < 0 ? (size_t)-1 : (size_t)prec);
            break;
        case 'c':
            digits[0] = (char)__builtin_va_arg(ap, int);
            str = digits, len = 1;
            break;
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X': {
            unsigned long long v;
            int neg = 0, i = (int)sizeof digits;
            unsigned base = *fmt == 'x' || *fmt == 'X' ? 16u : 10u;
            const char *set = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";

            if (*fmt == 'd' || *fmt == 'i') {
                long long s = lng ? __builtin_va_arg(ap, long long) : __builtin_va_arg(ap, int);

                neg = s < 0;
                v = neg ? 0ull - (unsigned long long)s : (unsigned long long)s;
            } else {
                v = lng ? __builtin_va_arg(ap, unsigned long long) : __builtin_va_arg(ap, unsigned);
            }
            do
                digits[--i] = set[v % base];
            while ((v /= base) != 0);
            if (neg)
                digits[--i] = '-';
            str = digits + i, len = (int)sizeof digits - i;
            break;
        }
        case '%':
            str = "%", len = 1;
            break;
        default:
            return -1; /* a format this subset does not know: refuse rather than guess */
        }
        if (!left)
            fmt_pad(&f, width - len, zero && *fmt != 's' ? '0' : ' ');
        for (int i = 0; i < len; i++)
            fmt_put(&f, str[i]);
        if (left)
            fmt_pad(&f, width - len, ' ');
    }
    if (cap)
        out[f.n < cap ? f.n : cap - 1] = '\0';
    return (int)f.n;
}

int snprintf(char *out, size_t cap, const char *fmt, ...)
{
    __builtin_va_list ap;
    int n;

    __builtin_va_start(ap, fmt);
    n = vsnprintf(out, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}
