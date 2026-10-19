#ifndef DELTA_OBJECT_H
#define DELTA_OBJECT_H

#include <delta/types.h>

#define MAX_CAPS 32

typedef struct Process Process;
typedef struct Endpoint Endpoint;

typedef enum {
    OBJ_NONE = 0,
    OBJ_CONSOLE = 1,
    OBJ_ENDPOINT = 2,
    OBJ_PROCESS = 3,
    OBJ_MEMORY = 4,
} ObjectType;

enum {
    RIGHT_READ    = 1u << 0,
    RIGHT_WRITE   = 1u << 1,
    RIGHT_SEND    = 1u << 2,
    RIGHT_RECV    = 1u << 3,
    RIGHT_EXEC    = 1u << 4,
    RIGHT_ADMIN   = 1u << 5,
    RIGHT_TRANSFER= 1u << 6,
    RIGHT_ALL     = 0x7Fu,
};

typedef struct Object {
    u32 type;
    u32 refs;
    u64 id;
    union {
        Endpoint *endpoint;
        Process *process;
    } u;
} Object;

typedef struct {
    Object *object;
    u32 rights;
    u32 pad;
} Cap;

void objects_init(void);
Object *object_create(ObjectType type);
void object_ref(Object *obj);
void object_unref(Object *obj);
Object *object_console(void);

int cap_grant(Process *p, Object *obj, u32 rights);
Object *cap_get(Process *p, u64 handle, ObjectType type, u32 rights);
bool cap_rights(Process *p, u64 handle, u32 *rights, Object **obj_out);
bool cap_close(Process *p, u64 handle);
int cap_dup(Process *p, u64 handle, u32 rights);
int cap_count(Process *p);
void cap_close_all(Process *p);

#endif
