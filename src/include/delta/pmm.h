#ifndef DELTA_PMM_H
#define DELTA_PMM_H

#include <delta/bootinfo.h>
#include <delta/types.h>

#define PAGE_SIZE       4096ULL
#define PMM_MAX_PHYS    0x40000000ULL       /* first 1 GiB is identity mapped */
#define PMM_MAX_FRAMES  (PMM_MAX_PHYS / PAGE_SIZE)

void pmm_init(const BootInfo *bi);
u64  pmm_alloc_frame(void);
u64  pmm_alloc_frame_zeroed(void);
void pmm_free_frame(u64 phys);
void pmm_reserve(u64 base, u64 length);
u64  pmm_total_frames(void);
u64  pmm_free_frames(void);
bool pmm_owns(u64 phys);

#endif
