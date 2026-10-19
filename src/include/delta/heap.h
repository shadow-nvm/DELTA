#ifndef DELTA_HEAP_H
#define DELTA_HEAP_H

#include <delta/types.h>

void  heap_init(u64 base, u64 size);
void *kmalloc(u64 size);
void *kzalloc(u64 size);
void *krealloc(void *ptr, u64 size);
void  kfree(void *ptr);
u64   heap_used(void);
u64   heap_free(void);

#endif
