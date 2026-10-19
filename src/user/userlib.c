#include "user.h"

void *memcpy(void *dst, const void *src, usize n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, usize n)
{
    u8 *d = dst;
    while (n--) {
        *d++ = (u8)c;
    }
    return dst;
}

usize strlen(const char *s)
{
    usize n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, usize n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n) {
        return 0;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

void putc(char c)
{
    sys_write(HANDLE_STDOUT, &c, 1);
}

void puts(const char *s)
{
    sys_write(HANDLE_STDOUT, s, strlen(s));
}

static u32 format_u64(u64 value, char *out, u32 base, bool upper)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    char rev[32];
    u32 n = 0;
    if (value == 0) {
        rev[n++] = '0';
    } else {
        while (value) {
            rev[n++] = digits[value % base];
            value /= base;
        }
    }
    for (u32 i = 0; i < n; i++) {
        out[i] = rev[n - 1 - i];
    }
    return n;
}

static void emit_padded(const char *s, u32 n, u32 width, bool left, char pad)
{
    if (!left) {
        while (width > n) {
            putc(pad);
            width--;
        }
    }
    for (u32 i = 0; i < n; i++) {
        putc(s[i]);
    }
    if (left) {
        while (width > n) {
            putc(' ');
            width--;
        }
    }
}

void printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            putc('%');
            fmt++;
            continue;
        }

        bool left = false;
        char pad = ' ';
        u32 width = 0;
        if (*fmt == '-') {
            left = true;
            fmt++;
        } else if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (u32)(*fmt - '0');
            fmt++;
        }

        bool wide = false;
        if (*fmt == 'l') {
            wide = true;
            fmt++;
            if (*fmt == 'l') {
                fmt++;
            }
        }

        char tmp[48];
        u32 n = 0;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            emit_padded(s, (u32)strlen(s), width, left, pad);
            break;
        }
        case 'c':
            tmp[0] = (char)__builtin_va_arg(ap, int);
            emit_padded(tmp, 1, width, left, pad);
            break;
        case 'd':
        case 'i': {
            i64 v = wide ? __builtin_va_arg(ap, long long)
                         : __builtin_va_arg(ap, int);
            u64 mag;
            if (v < 0) {
                tmp[n++] = '-';
                mag = (u64)(-(v + 1)) + 1;
            } else {
                mag = (u64)v;
            }
            n += format_u64(mag, tmp + n, 10, false);
            emit_padded(tmp, n, width, left, pad);
            break;
        }
        case 'u': {
            u64 v = wide ? __builtin_va_arg(ap, unsigned long long)
                         : __builtin_va_arg(ap, unsigned int);
            n = format_u64(v, tmp, 10, false);
            emit_padded(tmp, n, width, left, pad);
            break;
        }
        case 'x':
        case 'X': {
            u64 v = wide ? __builtin_va_arg(ap, unsigned long long)
                         : __builtin_va_arg(ap, unsigned int);
            n = format_u64(v, tmp, 16, *fmt == 'X');
            emit_padded(tmp, n, width, left, pad);
            break;
        }
        case 'p': {
            u64 v = (u64)__builtin_va_arg(ap, void *);
            tmp[n++] = '0';
            tmp[n++] = 'x';
            n += format_u64(v, tmp + n, 16, false);
            emit_padded(tmp, n, width, left, pad);
            break;
        }
        default:
            putc('%');
            putc(*fmt);
            break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

int read_line(char *buf, usize cap)
{
    usize n = 0;
    while (n + 1 < cap) {
        char c;
        long got = sys_read(HANDLE_STDIN, &c, 1);
        if (got != 1) {
            continue;
        }
        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            putc('\n');
            break;
        }
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                puts("\b \b");
            }
            continue;
        }
        if ((u8)c < 0x20) {
            continue;
        }
        buf[n++] = c;
        putc(c);
    }
    buf[n] = 0;
    return (int)n;
}
