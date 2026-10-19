#include <delta/timer.h>
#include <delta/io.h>

static volatile u64 g_ticks;

void timer_init(void)
{
    u32 divisor = 1193182u / TIMER_HZ;

    outb(0x43, 0x36);                  /* channel 0, lo/hi, mode 3 */
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));
    g_ticks = 0;
}

void timer_tick(void)
{
    g_ticks++;
}

u64 timer_ticks(void)
{
    return g_ticks;
}

u64 timer_ms(void)
{
    return (g_ticks * 1000) / TIMER_HZ;
}
