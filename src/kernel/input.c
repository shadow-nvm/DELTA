#include <delta/input.h>

#define INPUT_SIZE 512

static char g_buf[INPUT_SIZE];
static volatile u32 g_head;
static volatile u32 g_tail;
static void (*g_wake)(void);

void input_init(void)
{
    g_head = 0;
    g_tail = 0;
    g_wake = 0;
}

void input_set_wake(void (*cb)(void))
{
    g_wake = cb;
}

void input_push(char c)
{
    u32 next = (g_head + 1) % INPUT_SIZE;
    if (next == g_tail) {
        return; /* drop on overflow rather than corrupting the queue */
    }
    g_buf[g_head] = c;
    g_head = next;
    if (g_wake) {
        g_wake();
    }
}

int input_pop(void)
{
    if (g_tail == g_head) {
        return -1;
    }
    char c = g_buf[g_tail];
    g_tail = (g_tail + 1) % INPUT_SIZE;
    return (int)(u8)c;
}

u32 input_count(void)
{
    if (g_head >= g_tail) {
        return g_head - g_tail;
    }
    return INPUT_SIZE - g_tail + g_head;
}
