#include <delta/paging.h>
#include <delta/console.h>
#include <delta/io.h>
#include <delta/pmm.h>
#include <delta/string.h>

static u64 g_kernel_pml4;

static u64 *table(u64 phys)
{
    return (u64 *)(uintptr_t)phys;
}

static inline u64 index_of(u64 virt, u32 level)
{
    return (virt >> (12 + 9 * level)) & 0x1FF;
}

void vmm_init(void)
{
    g_kernel_pml4 = read_cr3() & ~0xFFFULL;
    kprintf("[mem] VMM root at %p (4-level paging, identity low 1 GiB)\n",
            (void *)g_kernel_pml4);
}

u64 vmm_kernel_space(void)
{
    return g_kernel_pml4;
}

u64 vmm_create_space(void)
{
    u64 pml4 = pmm_alloc_frame_zeroed();
    if (!pml4) {
        return 0;
    }
    u64 *new_root = table(pml4);
    u64 *kernel_root = table(g_kernel_pml4);
    /* Share the low kernel identity map. User mappings intentionally live in
     * PML4 slot 1, so a process cannot collide with kernel addresses. */
    new_root[0] = kernel_root[0];
    return pml4;
}

bool vmm_map_page(u64 pml4_phys, u64 virt, u64 phys, u64 flags)
{
    if ((virt & 0xFFF) || (phys & 0xFFF)) {
        return false;
    }

    u64 *pml4 = table(pml4_phys);
    u64 e = pml4[index_of(virt, 3)];
    if (!(e & PAGE_PRESENT)) {
        u64 t = pmm_alloc_frame_zeroed();
        if (!t) {
            return false;
        }
        pml4[index_of(virt, 3)] = t | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        e = pml4[index_of(virt, 3)];
    }
    if (e & PAGE_HUGE) {
        return false;
    }

    u64 *pdpt = table(e & ~0xFFFULL);
    e = pdpt[index_of(virt, 2)];
    if (!(e & PAGE_PRESENT)) {
        u64 t = pmm_alloc_frame_zeroed();
        if (!t) {
            return false;
        }
        pdpt[index_of(virt, 2)] = t | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        e = pdpt[index_of(virt, 2)];
    }
    if (e & PAGE_HUGE) {
        return false;
    }

    u64 *pd = table(e & ~0xFFFULL);
    e = pd[index_of(virt, 1)];
    if (!(e & PAGE_PRESENT)) {
        u64 t = pmm_alloc_frame_zeroed();
        if (!t) {
            return false;
        }
        pd[index_of(virt, 1)] = t | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        e = pd[index_of(virt, 1)];
    }
    if (e & PAGE_HUGE) {
        return false;
    }

    u64 *pt = table(e & ~0xFFFULL);
    pt[index_of(virt, 0)] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT;
    return true;
}

bool vmm_map_user_alloc(u64 pml4_phys, u64 virt, u64 flags)
{
    u64 frame = pmm_alloc_frame_zeroed();
    if (!frame) {
        return false;
    }
    if (!vmm_map_page(pml4_phys, virt, frame, flags | PAGE_USER)) {
        pmm_free_frame(frame);
        return false;
    }
    return true;
}

u64 vmm_phys_of(u64 pml4_phys, u64 virt)
{
    u64 *pml4 = table(pml4_phys);
    u64 e = pml4[index_of(virt, 3)];
    if (!(e & PAGE_PRESENT)) {
        return 0;
    }
    if (e & PAGE_HUGE) {
        return (e & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
    }
    u64 *pdpt = table(e & ~0xFFFULL);
    e = pdpt[index_of(virt, 2)];
    if (!(e & PAGE_PRESENT)) {
        return 0;
    }
    if (e & PAGE_HUGE) {
        return (e & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
    }
    u64 *pd = table(e & ~0xFFFULL);
    e = pd[index_of(virt, 1)];
    if (!(e & PAGE_PRESENT)) {
        return 0;
    }
    if (e & PAGE_HUGE) {
        return (e & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
    }
    u64 *pt = table(e & ~0xFFFULL);
    e = pt[index_of(virt, 0)];
    if (!(e & PAGE_PRESENT)) {
        return 0;
    }
    return (e & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFF);
}

bool vmm_user_range_ok(u64 pml4_phys, u64 virt, u64 len, bool write)
{
    if (len == 0) {
        return true;
    }
    if (virt < USER_BASE || virt + len < virt || virt + len > USER_TOP) {
        return false;
    }
    u64 first = ALIGN_DOWN(virt, PAGE_SIZE);
    u64 last = ALIGN_DOWN(virt + len - 1, PAGE_SIZE);
    for (u64 va = first; va <= last; va += PAGE_SIZE) {
        u64 *pml4 = table(pml4_phys);
        u64 e = pml4[index_of(va, 3)];
        if (!(e & PAGE_PRESENT) || !(e & PAGE_USER)) {
            return false;
        }
        u64 *pdpt = table(e & ~0xFFFULL);
        e = pdpt[index_of(va, 2)];
        if (!(e & PAGE_PRESENT) || !(e & PAGE_USER)) {
            return false;
        }
        u64 *pd = table(e & ~0xFFFULL);
        e = pd[index_of(va, 1)];
        if (!(e & PAGE_PRESENT) || !(e & PAGE_USER)) {
            return false;
        }
        u64 *pt = table(e & ~0xFFFULL);
        e = pt[index_of(va, 0)];
        if (!(e & PAGE_PRESENT) || !(e & PAGE_USER)) {
            return false;
        }
        if (write && !(e & PAGE_WRITE)) {
            return false;
        }
    }
    return true;
}

void vmm_destroy_space(u64 pml4_phys)
{
    if (!pml4_phys || pml4_phys == g_kernel_pml4) {
        return;
    }
    u64 *pml4 = table(pml4_phys);
    u64 pml4e = pml4[1];
    if (pml4e & PAGE_PRESENT) {
        u64 *pdpt = table(pml4e & ~0xFFFULL);
        for (u32 i = 0; i < 512; i++) {
            u64 pdpte = pdpt[i];
            if (!(pdpte & PAGE_PRESENT)) {
                continue;
            }
            u64 *pd = table(pdpte & ~0xFFFULL);
            for (u32 j = 0; j < 512; j++) {
                u64 pde = pd[j];
                if (!(pde & PAGE_PRESENT) || (pde & PAGE_HUGE)) {
                    continue;
                }
                u64 *pt = table(pde & ~0xFFFULL);
                for (u32 k = 0; k < 512; k++) {
                    u64 pte = pt[k];
                    if (pte & PAGE_PRESENT) {
                        pmm_free_frame(pte & ~0xFFFULL);
                    }
                }
                pmm_free_frame(pde & ~0xFFFULL);
            }
            pmm_free_frame(pdpte & ~0xFFFULL);
        }
        pmm_free_frame(pml4e & ~0xFFFULL);
    }
    pmm_free_frame(pml4_phys);
}

void vmm_switch(u64 pml4_phys)
{
    if (read_cr3() != pml4_phys) {
        write_cr3(pml4_phys);
    }
}

void vmm_flush(void)
{
    write_cr3(read_cr3());
}
