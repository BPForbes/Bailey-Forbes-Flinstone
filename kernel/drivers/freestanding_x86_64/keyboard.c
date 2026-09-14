#include "keyboard.h"
#include "ioport.h"

#define KB_DATA 0x60
#define KB_STATUS 0x64
#define QUEUE 32

static char s_queue[QUEUE];
static unsigned s_head;
static unsigned s_tail;
static int s_shift;

static const char s_plain[89] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char s_shifted[89] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void push(char c)
{
    unsigned next = (s_head + 1u) % QUEUE;
    if (next == s_tail)
        return;
    s_queue[s_head] = c;
    s_head = next;
}

void fl_fs_kbd_init(void)
{
    s_head = s_tail = 0;
    s_shift = 0;
    (void)fl_fs_inb(KB_STATUS);
}

void fl_fs_kbd_irq(void)
{
    uint8_t status = fl_fs_inb(KB_STATUS);
    if ((status & 1u) == 0)
        return;
    uint8_t sc = fl_fs_inb(KB_DATA);
    if (sc == 0x2a || sc == 0x36) {
        s_shift = 1;
        return;
    }
    if (sc == 0xaa || sc == 0xb6) {
        s_shift = 0;
        return;
    }
    if (sc == 0x0e) {
        push('\b');
        return;
    }
    if (sc & 0x80u)
        return;
    if (sc >= 89)
        return;
    char c = s_shift ? s_shifted[sc] : s_plain[sc];
    if (c)
        push(c);
}

int fl_fs_kbd_getc(char *out)
{
    if (s_head == s_tail)
        return 0;
    *out = s_queue[s_tail];
    s_tail = (s_tail + 1u) % QUEUE;
    return 1;
}
