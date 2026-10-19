#include <delta/ipc.h>
#include <delta/event.h>
#include <delta/heap.h>
#include <delta/process.h>
#include <delta/string.h>

Endpoint *endpoint_create(void)
{
    Endpoint *ep = kzalloc(sizeof(Endpoint));
    return ep;
}

void endpoint_destroy(Endpoint *ep)
{
    if (ep) {
        kfree(ep);
    }
}

bool endpoint_has_message(Endpoint *ep)
{
    return ep && ep->count > 0;
}

long endpoint_send(Endpoint *ep, u32 sender_pid, const void *data, u32 len)
{
    if (!ep || !data || len > IPC_MSG_MAX) {
        return -22; /* EINVAL */
    }

    /* Direct rendezvous when a receiver is already parked on this endpoint. */
    Process *r = ep->receiver;
    if (r && r->state == PROC_BLOCKED && r->wait_kind == WAIT_IPC &&
        r->wait_endpoint == ep) {
        if (len > r->wait_len) {
            return -90; /* EMSGSIZE */
        }
        if (!process_copy_to(r, r->wait_buf, data, len)) {
            return -14; /* EFAULT */
        }
        TrapFrame *frame = (TrapFrame *)(uintptr_t)r->saved_rsp;
        if (frame) {
            frame->rax = len;
        }
        r->wait_endpoint = 0;
        r->wait_buf = 0;
        r->wait_len = 0;
        r->wait_kind = WAIT_NONE;
        process_wake(r);
        ep->receiver = 0;
        event_log(EVT_IPC_SEND, sender_pid, r->pid, len);
        return (long)len;
    }

    if (ep->count >= IPC_QUEUE_LEN) {
        return -11; /* EAGAIN */
    }
    IpcMessage *m = &ep->msgs[ep->head];
    m->sender = sender_pid;
    m->len = len;
    memcpy(m->data, data, len);
    ep->head = (ep->head + 1) % IPC_QUEUE_LEN;
    ep->count++;
    event_log(EVT_IPC_SEND, sender_pid, 0, len);
    return (long)len;
}

long endpoint_recv(Endpoint *ep, void *out, u32 cap)
{
    if (!ep || !out) {
        return -22;
    }
    if (ep->count == 0) {
        return -11;
    }
    IpcMessage *m = &ep->msgs[ep->tail];
    if (m->len > cap) {
        return -90;
    }
    memcpy(out, m->data, m->len);
    ep->tail = (ep->tail + 1) % IPC_QUEUE_LEN;
    ep->count--;
    return (long)m->len;
}
