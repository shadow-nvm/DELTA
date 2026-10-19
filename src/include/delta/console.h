#ifndef DELTA_CONSOLE_H
#define DELTA_CONSOLE_H

#include <delta/types.h>

/* VGA text-mode colours. */
enum {
    COL_BLACK = 0,
    COL_BLUE = 1,
    COL_GREEN = 2,
    COL_CYAN = 3,
    COL_RED = 4,
    COL_MAGENTA = 5,
    COL_BROWN = 6,
    COL_LGRAY = 7,
    COL_DGRAY = 8,
    COL_LBLUE = 9,
    COL_LGREEN = 10,
    COL_LCYAN = 11,
    COL_LRED = 12,
    COL_LMAGENTA = 13,
    COL_YELLOW = 14,
    COL_WHITE = 15,
};

#define VGA_ATTR(fg, bg) ((u8)((fg) | ((bg) << 4)))

void console_init(u64 vga_addr, u32 cols, u32 rows);
void console_clear(void);
void console_set_attr(u8 attr);
u8   console_attr(void);
void console_putc(char c);
void console_write(const char *s);
void console_write_len(const char *s, usize len);
void console_write_attr(const char *s, u8 attr);

void kprintf(const char *fmt, ...);
void kpanic(const char *fmt, ...) NORETURN;

#endif
