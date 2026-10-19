#include <delta/pmm.h>
#include <delta/console.h>
#include <delta/io.h>
#include <delta/string.h>

extern u8 __kernel_end[];

#define BITMAP_WORDS (PMM_MAX_FRAMES / 64)

static u64 g_bitmap[BITMAP_WORDS];
static u64 g_free_frames;
static u64 g_total_frames;
static u64 g_hint;

static inline bool bit_get(u64 frame)
{
    return (g_bitmap[frame / 64] >> (frame % 64)) & 1;
}

static inline void bit_set(u64 frame)
{
    g_bitmap[frame / 64] |= 1ULL << (frame % 64);
}

static inline void bit_clear(u64 frame)
{
    g_bitmap[frame / 64] &= ~(1ULL << (frame % 64));
}

static void mark_used(u64 frame)
{
    if (frame < PMM_MAX_FRAMES && !bit_get(frame)) {
        bit_set(frame);
        g_free_frames--;
    }
}

static void mark_free(u64 frame)
{
    if (frame < PMM_MAX_FRAMES && bit_get(frame)) {
        bit_clear(frame);
        g_free_frames++;
    }
}

void pmm_reserve(u64 base, u64 length)
{
    if (!length) {
        return;
    }
    u64 start = ALIGN_DOWN(base, PAGE_SIZE) / PAGE_SIZE;
    u64 end = ALIGN_UP(base + length, PAGE_SIZE) / PAGE_SIZE;
    if (end > PMM_MAX_FRAMES) {
        end = PMM_MAX_FRAMES;
    }
    for (u64 f = start; f < end; f++) {
        mark_used(f);
    }
}

void pmm_init(const BootInfo *bi)
{
    memset(g_bitmap, 0xFF, sizeof(g_bitmap));
    g_free_frames = 0;
    g_total_frames = 0;
    g_hint = 0;

    const E820Entry *map = (const E820Entry *)(uintptr_t)bi->e820_addr;
    for (u32 i = 0; i < bi->e820_count; i++) {
        if (map[i].type != E820_USABLE) {
            continue;
        }
        u64 base = map[i].base;
        u64 end = map[i].base + map[i].length;
        if (end <= base) {
            continue;
        }
        if (base < MB) {
            base = MB;
        }
        if (end > PMM_MAX_PHYS) {
            end = PMM_MAX_PHYS;
        }
        if (end <= base) {
            continue;
        }
        u64 first = ALIGN_UP(base, PAGE_SIZE) / PAGE_SIZE;
        u64 last = ALIGN_DOWN(end, PAGE_SIZE) / PAGE_SIZE;
        if (last > first) {
            g_total_frames += last - first;
        }
        for (u64 f = first; f < last; f++) {
            mark_free(f);
        }
    }

    /* Firmware, the boot sector, stage2, E820 data, bootinfo, the VGA window,
     * and the page tables installed by stage2 all live below 1 MiB. */
    pmm_reserve(0, MB);
    /* The kernel image and its BSS. */
    pmm_reserve(0x100000, (u64)(uintptr_t)__kernel_end - 0x100000);
    /* Guard the legacy VGA aperture and BIOS ROM area. */
    pmm_reserve(0xA0000, 0x60000);

    kprintf("[mem] PMM: %llu free frames (%llu MiB) of %llu usable frames\n",
            g_free_frames, (g_free_frames * PAGE_SIZE) / MB, g_total_frames);
}

u64 pmm_alloc_frame(void)
{
    u64 flags = irq_save();
    for (u64 pass = 0; pass < BITMAP_WORDS; pass++) {
        u64 word = (g_hint + pass) % BITMAP_WORDS;
        if (g_bitmap[word] != ~0ULL) {
            u32 bit = (u32)__builtin_ctzll(~g_bitmap[word]);
            u64 frame = word * 64 + bit;
            if (frame >= PMM_MAX_FRAMES) {
                continue;
            }
            bit_set(frame);
            g_free_frames--;
            g_hint = (word + 1) % BITMAP_WORDS;
            irq_restore(flags);
            return frame * PAGE_SIZE;
        }
    }
    irq_restore(flags);
    return 0;
}

u64 pmm_alloc_frame_zeroed(void)
{
    u64 phys = pmm_alloc_frame();
    if (phys) {
        memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
    }
    return phys;
}

void pmm_free_frame(u64 phys)
{
    if ((phys & (PAGE_SIZE - 1)) != 0 || phys < MB || phys >= PMM_MAX_PHYS) {
        return;
    }
    u64 frame = phys / PAGE_SIZE;
    u64 flags = irq_save();
    mark_free(frame);
    irq_restore(flags);
}

bool pmm_owns(u64 phys)
{
    return phys >= MB && phys < PMM_MAX_PHYS;
}

u64 pmm_total_frames(void)
{
    return g_total_frames;
}

u64 pmm_free_frames(void)
{
    return g_free_frames;
}
