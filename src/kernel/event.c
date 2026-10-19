#include <delta/event.h>
#include <delta/io.h>
#include <delta/string.h>
#include <delta/timer.h>

static DeltaEvent g_ring[EVENT_RING_SIZE];
static u32 g_head;
static u32 g_count;
static u64 g_seq;

void event_init(void)
{
    memset(g_ring, 0, sizeof(g_ring));
    g_head = 0;
    g_count = 0;
    g_seq = 1;
}

void event_log(u32 type, u64 a, u64 b, u64 c)
{
    u64 flags = irq_save();
    DeltaEvent *e = &g_ring[g_head];
    e->seq = g_seq++;
    e->tick = timer_ticks();
    e->type = type;
    e->a = a;
    e->b = b;
    e->c = c;
    g_head = (g_head + 1) % EVENT_RING_SIZE;
    if (g_count < EVENT_RING_SIZE) {
        g_count++;
    }
    irq_restore(flags);
}

u32 event_total(void)
{
    return g_count;
}

u32 event_copy(DeltaEvent *out, u32 max, bool newest)
{
    if (!out || !max) {
        return 0;
    }
    u32 n = g_count < max ? g_count : max;
    for (u32 i = 0; i < n; i++) {
        u32 slot;
        if (newest) {
            slot = (g_head + EVENT_RING_SIZE - n + i) % EVENT_RING_SIZE;
        } else {
            slot = (g_head + EVENT_RING_SIZE - g_count + i) % EVENT_RING_SIZE;
        }
        out[i] = g_ring[slot];
    }
    return n;
}
