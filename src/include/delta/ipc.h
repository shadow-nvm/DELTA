#ifndef DELTA_IPC_H
#define DELTA_IPC_H

#include <delta/types.h>

#define IPC_MSG_MAX   128
#define IPC_QUEUE_LEN 16

typedef struct {
    u32 sender;
    u32 len;
    u8 data[IPC_MSG_MAX];
} IpcMessage;

struct Process;

typedef struct Endpoint {
    u32 head;
    u32 tail;
    u32 count;
    struct Process *receiver;
    IpcMessage msgs[IPC_QUEUE_LEN];
} Endpoint;

Endpoint *endpoint_create(void);
void endpoint_destroy(Endpoint *ep);
long endpoint_send(Endpoint *ep, u32 sender_pid, const void *data, u32 len);
long endpoint_recv(Endpoint *ep, void *out, u32 cap);
bool endpoint_has_message(Endpoint *ep);

#endif
