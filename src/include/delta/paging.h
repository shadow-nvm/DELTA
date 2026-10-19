#ifndef DELTA_PAGING_H
#define DELTA_PAGING_H

#include <delta/types.h>

#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_WRITE     (1ULL << 1)
#define PAGE_USER      (1ULL << 2)
#define PAGE_WT        (1ULL << 3)
#define PAGE_CD        (1ULL << 4)
#define PAGE_NX        (1ULL << 63)
#define PAGE_HUGE      (1ULL << 7)

/* Every process sees the kernel's low identity map plus a private user
 * region one PML4 slot above it. This keeps user pointers far away from
 * kernel data while avoiding a global higher-half split for the prototype. */
#define USER_BASE       0x0000008000000000ULL
#define USER_TOP        0x0000008040000000ULL
#define USER_STACK_TOP  (USER_BASE + 0x00800000ULL)
#define USER_STACK_PAGES 16

void  vmm_init(void);
u64   vmm_kernel_space(void);
u64   vmm_create_space(void);
void  vmm_destroy_space(u64 pml4_phys);
bool  vmm_map_page(u64 pml4_phys, u64 virt, u64 phys, u64 flags);
bool  vmm_map_user_alloc(u64 pml4_phys, u64 virt, u64 flags);
bool  vmm_user_range_ok(u64 pml4_phys, u64 virt, u64 len, bool write);
u64   vmm_phys_of(u64 pml4_phys, u64 virt);
void  vmm_switch(u64 pml4_phys);
void  vmm_flush(void);

#endif
