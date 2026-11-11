#include "fwcfg.h"
#include "ioport.h"

#define FW_CFG_SEL 0x510
#define FW_CFG_DATA 0x511
#define FW_CFG_SIGNATURE 0x0000
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_NAME_MAX 56

static void select_item(uint16_t key)
{
    fl_fs_outw(FW_CFG_SEL, key);
}

static uint32_t read_be(unsigned bytes)
{
    uint32_t v = 0;

    while (bytes--)
        v = (v << 8) | fl_fs_inb(FW_CFG_DATA);
    return v;
}

int fl_fs_fwcfg_present(void)
{
    select_item(FW_CFG_SIGNATURE);
    return fl_fs_inb(FW_CFG_DATA) == 'Q' && fl_fs_inb(FW_CFG_DATA) == 'E' && fl_fs_inb(FW_CFG_DATA) == 'M' &&
           fl_fs_inb(FW_CFG_DATA) == 'U';
}

/* The directory is a big-endian count, then per file: size u32, select u16, reserved u16, name[56]. */
static int find(const char *name, uint16_t *sel, uint32_t *size)
{
    uint32_t count;
    size_t n = 0;

    while (name[n])
        n++;
    if (n == 0 || n >= FW_CFG_NAME_MAX)
        return -1;
    select_item(FW_CFG_FILE_DIR);
    count = read_be(4);
    for (uint32_t i = 0; i < count && i < 64; i++) {
        uint32_t sz = read_be(4);
        uint16_t s = (uint16_t)read_be(2);
        int same = 1;

        (void)read_be(2);
        for (unsigned j = 0; j < FW_CFG_NAME_MAX; j++) {
            char c = (char)fl_fs_inb(FW_CFG_DATA);

            if (j <= n && c != name[j]) /* compares the name and its terminator */
                same = 0;
        }
        if (same) {
            *sel = s;
            *size = sz;
            return 0;
        }
    }
    return -1;
}

int fl_fs_fwcfg_read(const char *name, uint32_t offset, uint8_t *out, size_t len)
{
    uint16_t sel;
    uint32_t size;

    if (!name || !out || !fl_fs_fwcfg_present() || find(name, &sel, &size) != 0 || offset > size ||
        len > (size_t)(size - offset))
        return -1;
    select_item(sel);
    for (uint32_t i = 0; i < offset; i++)
        (void)fl_fs_inb(FW_CFG_DATA); /* the data port reads forward only */
    for (size_t i = 0; i < len; i++)
        out[i] = fl_fs_inb(FW_CFG_DATA);
    return 0;
}
