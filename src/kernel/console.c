#include <delta/console.h>
#include <delta/serial.h>
#include <delta/string.h>

static volatile u16 *g_vga;
static u32 g_cols;
static u32 g_rows;
static u32 g_x;
static u32 g_y;
static u8  g_attr = VGA_ATTR(COL_LGRAY, COL_BLACK);

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* ------------------------------------------------------------------------- */
/* VGA text console                                                          */
/* ------------------------------------------------------------------------- */

static void cursor_update(void)
{
    u16 pos = (u16)(g_y * g_cols + g_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

static void clear_row(u32 row)
{
    for (u32 x = 0; x < g_cols; x++) {
        g_vga[row * g_cols + x] = (u16)(' ' | ((u16)g_attr << 8));
    }
}

static void scroll(void)
{
    if (g_y < g_rows) {
        return;
    }
    for (u32 y = 1; y < g_rows; y++) {
        for (u32 x = 0; x < g_cols; x++) {
            g_vga[(y - 1) * g_cols + x] = g_vga[y * g_cols + x];
        }
    }
    clear_row(g_rows - 1);
    g_y = g_rows - 1;
}

void console_init(u64 vga_addr, u32 cols, u32 rows)
{
    g_vga = (volatile u16 *)(uintptr_t)vga_addr;
    g_cols = cols ? cols : 80;
    g_rows = rows ? rows : 25;
    g_x = 0;
    g_y = 0;
    serial_init();
    console_clear();
}

void console_set_attr(u8 attr)
{
    g_attr = attr;
}

u8 console_attr(void)
{
    return g_attr;
}

void console_clear(void)
{
    for (u32 y = 0; y < g_rows; y++) {
        clear_row(y);
    }
    g_x = 0;
    g_y = 0;
    cursor_update();
}

void console_putc(char c)
{
    serial_putc(c);

    if (c == '\n') {
        g_x = 0;
        g_y++;
    } else if (c == '\r') {
        g_x = 0;
    } else if (c == '\t') {
        u32 next = (g_x + 8) & ~7u;
        while (g_x < next && g_x < g_cols) {
            console_putc(' ');
        }
        return;
    } else if (c == '\b') {
        if (g_x > 0) {
            g_x--;
        }
        g_vga[g_y * g_cols + g_x] = (u16)(' ' | ((u16)g_attr << 8));
    } else {
        g_vga[g_y * g_cols + g_x] = (u16)((u8)c | ((u16)g_attr << 8));
        g_x++;
        if (g_x >= g_cols) {
            g_x = 0;
            g_y++;
        }
    }

    scroll();
    cursor_update();
}

void console_write_len(const char *s, usize len)
{
    for (usize i = 0; i < len; i++) {
        console_putc(s[i]);
    }
}

void console_write(const char *s)
{
    while (*s) {
        console_putc(*s++);
    }
}

void console_write_attr(const char *s, u8 attr)
{
    u8 old = g_attr;
    g_attr = attr;
    console_write(s);
    g_attr = old;
}

/* ------------------------------------------------------------------------- */
/* formatted kernel output                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    char buf[80];
    u32 len;
    u32 width;
    char pad;
} PrintBuf;

static void pb_flush(PrintBuf *pb)
{
    console_write_len(pb->buf, pb->len);
    pb->len = 0;
}

static void pb_char(PrintBuf *pb, char c)
{
    if (pb->len == sizeof(pb->buf)) {
        pb_flush(pb);
    }
    pb->buf[pb->len++] = c;
}

static void pb_repeat(PrintBuf *pb, char c, u32 n)
{
    while (n--) {
        pb_char(pb, c);
    }
}

static void pb_unsigned(PrintBuf *pb, u64 value, u32 base, bool upper)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    char tmp[32];
    u32 n = 0;

    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value) {
            tmp[n++] = digits[value % base];
            value /= base;
        }
    }
    if (pb->width > n) {
        pb_repeat(pb, pb->pad, pb->width - n);
    }
    while (n) {
        pb_char(pb, tmp[--n]);
    }
}

static void pb_signed(PrintBuf *pb, i64 value)
{
    if (value < 0) {
        pb_char(pb, '-');
        pb_unsigned(pb, (u64)(-(value + 1)) + 1, 10, false);
    } else {
        pb_unsigned(pb, (u64)value, 10, false);
    }
}

static void kprintf_va(const char *fmt, va_list ap)
{
    PrintBuf pb = { .len = 0, .width = 0, .pad = ' ' };

    while (*fmt) {
        if (*fmt != '%') {
            pb_char(&pb, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            pb_char(&pb, '%');
            fmt++;
            continue;
        }

        pb.width = 0;
        pb.pad = ' ';
        if (*fmt == '0') {
            pb.pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            pb.width = pb.width * 10 + (u32)(*fmt - '0');
            fmt++;
        }

        bool is_long = false;
        bool is_long_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') {
                is_long_long = true;
                fmt++;
            }
        } else if (*fmt == 'z') {
            is_long = true;
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            pb_char(&pb, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            u32 n = 0;
            while (s[n]) {
                n++;
            }
            if (pb.width > n) {
                pb_repeat(&pb, pb.pad, pb.width - n);
            }
            for (u32 i = 0; i < n; i++) {
                pb_char(&pb, s[i]);
            }
            break;
        }
        case 'd':
        case 'i': {
            i64 v;
            if (is_long_long) {
                v = va_arg(ap, long long);
            } else if (is_long) {
                v = va_arg(ap, long);
            } else {
                v = va_arg(ap, int);
            }
            pb_signed(&pb, v);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            u64 v;
            if (is_long_long) {
                v = va_arg(ap, unsigned long long);
            } else if (is_long) {
                v = va_arg(ap, unsigned long);
            } else {
                v = va_arg(ap, unsigned int);
            }
            u32 base = (*fmt == 'u') ? 10 : 16;
            pb_unsigned(&pb, v, base, *fmt == 'X');
            break;
        }
        case 'p': {
            u64 v = (u64)(uintptr_t)va_arg(ap, void *);
            pb_char(&pb, '0');
            pb_char(&pb, 'x');
            u32 old_width = pb.width;
            pb.width = 16;
            pb.pad = '0';
            pb_unsigned(&pb, v, 16, false);
            pb.width = old_width;
            break;
        }
        case 0:
            pb_flush(&pb);
            return;
        default:
            pb_char(&pb, '%');
            pb_char(&pb, *fmt);
            break;
        }
        fmt++;
    }
    pb_flush(&pb);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kprintf_va(fmt, ap);
    va_end(ap);
}

void kpanic(const char *fmt, ...)
{
    __asm__ volatile("cli");
    console_set_attr(VGA_ATTR(COL_WHITE, COL_RED));
    console_write("\n\n  *** DELTA KERNEL PANIC ***\n  ");
    va_list ap;
    va_start(ap, fmt);
    kprintf_va(fmt, ap);
    va_end(ap);
    console_write("\n  system halted; power cycle required.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
