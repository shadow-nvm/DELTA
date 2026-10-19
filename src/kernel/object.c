#include <delta/object.h>
#include <delta/event.h>
#include <delta/heap.h>
#include <delta/ipc.h>
#include <delta/process.h>
#include <delta/string.h>

static Object g_console;
static u64 g_next_object_id = 1;

void objects_init(void)
{
    memset(&g_console, 0, sizeof(g_console));
    g_console.type = OBJ_CONSOLE;
    g_console.refs = 1;
    g_console.id = g_next_object_id++;
    g_next_object_id = 0x100;
}

Object *object_console(void)
{
    return &g_console;
}

Object *object_create(ObjectType type)
{
    Object *obj = kzalloc(sizeof(Object));
    if (!obj) {
        return 0;
    }
    obj->type = (u32)type;
    obj->refs = 1;
    obj->id = g_next_object_id++;
    if (type == OBJ_ENDPOINT) {
        obj->u.endpoint = endpoint_create();
        if (!obj->u.endpoint) {
            kfree(obj);
            return 0;
        }
    }
    return obj;
}

void object_ref(Object *obj)
{
    if (obj) {
        obj->refs++;
    }
}

void object_unref(Object *obj)
{
    if (!obj || obj == &g_console) {
        return;
    }
    if (obj->refs == 0) {
        return;
    }
    obj->refs--;
    if (obj->refs != 0) {
        return;
    }
    if (obj->type == OBJ_ENDPOINT && obj->u.endpoint) {
        endpoint_destroy(obj->u.endpoint);
        obj->u.endpoint = 0;
    }
    kfree(obj);
}

int cap_grant(Process *p, Object *obj, u32 rights)
{
    if (!p || !obj) {
        return -1;
    }
    for (u64 h = 1; h < MAX_CAPS; h++) {
        if (!p->caps[h].object) {
            p->caps[h].object = obj;
            p->caps[h].rights = rights;
            object_ref(obj);
            event_log(EVT_CAP_GRANT, (u64)p->pid, obj->id, rights);
            return (int)h;
        }
    }
    return -2; /* EMFILE */
}

Object *cap_get(Process *p, u64 handle, ObjectType type, u32 rights)
{
    if (!p || handle == 0 || handle >= MAX_CAPS) {
        return 0;
    }
    Cap *c = &p->caps[handle];
    if (!c->object) {
        return 0;
    }
    if ((c->rights & rights) != rights) {
        return 0;
    }
    if (type != OBJ_NONE && c->object->type != (u32)type) {
        return 0;
    }
    return c->object;
}

bool cap_rights(Process *p, u64 handle, u32 *rights, Object **obj_out)
{
    if (!p || handle == 0 || handle >= MAX_CAPS || !p->caps[handle].object) {
        return false;
    }
    if (rights) {
        *rights = p->caps[handle].rights;
    }
    if (obj_out) {
        *obj_out = p->caps[handle].object;
    }
    return true;
}

bool cap_close(Process *p, u64 handle)
{
    if (!p || handle == 0 || handle >= MAX_CAPS || !p->caps[handle].object) {
        return false;
    }
    Object *obj = p->caps[handle].object;
    p->caps[handle].object = 0;
    p->caps[handle].rights = 0;
    object_unref(obj);
    return true;
}

int cap_dup(Process *p, u64 handle, u32 rights)
{
    Object *obj = cap_get(p, handle, OBJ_NONE, 0);
    if (!obj) {
        return -1;
    }
    if (!rights) {
        rights = p->caps[handle].rights;
    }
    return cap_grant(p, obj, rights & p->caps[handle].rights);
}

int cap_count(Process *p)
{
    int n = 0;
    if (!p) {
        return 0;
    }
    for (u32 i = 1; i < MAX_CAPS; i++) {
        if (p->caps[i].object) {
            n++;
        }
    }
    return n;
}

void cap_close_all(Process *p)
{
    if (!p) {
        return;
    }
    for (u32 i = 1; i < MAX_CAPS; i++) {
        if (p->caps[i].object) {
            Object *obj = p->caps[i].object;
            p->caps[i].object = 0;
            p->caps[i].rights = 0;
            object_unref(obj);
        }
    }
}
