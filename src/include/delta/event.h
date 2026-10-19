#ifndef DELTA_EVENT_H
#define DELTA_EVENT_H

#include <delta/types.h>

#define EVENT_RING_SIZE 128
#define EVENT_MAX_BATCH 16

enum {
    EVT_BOOT = 1,
    EVT_CAP_GRANT,
    EVT_CAP_CLOSE,
    EVT_PROC_SPAWN,
    EVT_PROC_EXIT,
    EVT_IPC_SEND,
    EVT_IPC_RECV,
    EVT_FAULT,
    EVT_SCHED,
    EVT_POWER,
};

typedef struct {
    u64 seq;
    u64 tick;
    u32 type;
    u32 pad;
    u64 a;
    u64 b;
    u64 c;
} DeltaEvent;

void event_init(void);
void event_log(u32 type, u64 a, u64 b, u64 c);
u32  event_total(void);
u32  event_copy(DeltaEvent *out, u32 max, bool newest);

#endif
