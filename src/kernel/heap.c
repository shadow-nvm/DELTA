#include <delta/heap.h>
#include <delta/console.h>
#include <delta/io.h>
#include <delta/string.h>

#define HEAP_MAGIC 0x48454144u  /* "HEAP" */

typedef struct ALIGNED(16) HeapBlock {
    u32 magic;
    u32 free;
    u64 size;
    struct HeapBlock *next;
} HeapBlock;

static u8 *g_heap_base;
static u8 *g_heap_end;
static HeapBlock *g_free_list;
static u64 g_used;
static u64 g_free_bytes;

void heap_init(u64 base, u64 size)
{
    g_heap_base = (u8 *)(uintptr_t)base;
    g_heap_end = g_heap_base + size;
    g_used = 0;

    HeapBlock *b = (HeapBlock *)g_heap_base;
    b->magic = HEAP_MAGIC;
    b->free = 1;
    b->size = size - sizeof(HeapBlock);
    b->next = 0;
    g_free_list = b;
    g_free_bytes = b->size;

    kprintf("[mem] heap: %llu KiB at %p\n", size / KB, (void *)base);
}

static void insert_free(HeapBlock *b)
{
    HeapBlock *prev = 0;
    HeapBlock *cur = g_free_list;
    while (cur && cur < b) {
        prev = cur;
        cur = cur->next;
    }
    b->next = cur;
    if (prev) {
        prev->next = b;
    } else {
        g_free_list = b;
    }
}

static void coalesce(HeapBlock *b)
{
    if (b->next && (u8 *)b + sizeof(HeapBlock) + b->size == (u8 *)b->next) {
        HeapBlock *n = b->next;
        b->size += sizeof(HeapBlock) + n->size;
        b->next = n->next;
    }
    HeapBlock *prev = g_free_list;
    while (prev && prev->next != b) {
        prev = prev->next;
    }
    if (prev && (u8 *)prev + sizeof(HeapBlock) + prev->size == (u8 *)b) {
        prev->size += sizeof(HeapBlock) + b->size;
        prev->next = b->next;
    }
}

void *kmalloc(u64 size)
{
    if (!size) {
        size = 1;
    }
    size = ALIGN_UP(size, 16);
    u64 flags = irq_save();

    for (HeapBlock *b = g_free_list; b; b = b->next) {
        if (!b->free || b->size < size) {
            continue;
        }
        /* Remove from the free list. */
        HeapBlock **link = &g_free_list;
        while (*link && *link != b) {
            link = &(*link)->next;
        }
        if (*link == b) {
            *link = b->next;
        }

        u64 remaining = b->size - size;
        if (remaining >= sizeof(HeapBlock) + 32) {
            HeapBlock *split = (HeapBlock *)((u8 *)b + sizeof(HeapBlock) + size);
            split->magic = HEAP_MAGIC;
            split->free = 1;
            split->size = remaining - sizeof(HeapBlock);
            split->next = 0;
            insert_free(split);
            b->size = size;
        }

        b->magic = HEAP_MAGIC;
        b->free = 0;
        b->next = 0;
        g_used += b->size;
        g_free_bytes -= b->size;
        irq_restore(flags);
        return (void *)((u8 *)b + sizeof(HeapBlock));
    }

    irq_restore(flags);
    return 0;
}

void *kzalloc(u64 size)
{
    void *p = kmalloc(size);
    if (p) {
        memset(p, 0, size);
    }
    return p;
}

void *krealloc(void *ptr, u64 size)
{
    if (!ptr) {
        return kmalloc(size);
    }
    if (!size) {
        kfree(ptr);
        return 0;
    }
    HeapBlock *b = (HeapBlock *)((u8 *)ptr - sizeof(HeapBlock));
    if (b->magic != HEAP_MAGIC || b->free) {
        return 0;
    }
    if (b->size >= size) {
        return ptr;
    }
    void *n = kmalloc(size);
    if (!n) {
        return 0;
    }
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

void kfree(void *ptr)
{
    if (!ptr) {
        return;
    }
    u8 *p = (u8 *)ptr;
    if (p < g_heap_base + sizeof(HeapBlock) || p >= g_heap_end) {
        return;
    }
    HeapBlock *b = (HeapBlock *)(p - sizeof(HeapBlock));
    if (b->magic != HEAP_MAGIC || b->free) {
        return;
    }
    u64 flags = irq_save();
    b->free = 1;
    b->next = 0;
    g_used -= b->size;
    g_free_bytes += b->size;
    insert_free(b);
    coalesce(b);
    irq_restore(flags);
}

u64 heap_used(void)
{
    return g_used;
}

u64 heap_free(void)
{
    return g_free_bytes;
}
