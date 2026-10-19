#include <delta/bootinfo.h>
#include <delta/console.h>
#include <delta/event.h>
#include <delta/gdt.h>
#include <delta/heap.h>
#include <delta/interrupts.h>
#include <delta/io.h>
#include <delta/paging.h>
#include <delta/pmm.h>
#include <delta/process.h>
#include <delta/string.h>
#include <delta/timer.h>
#include <delta/version.h>

extern u8 __kernel_start[];
extern u8 __kernel_end[];

static const char *banner[] = {
    "          /\\ ",
    "         /  \\         D E L T A",
    "        / /\\ \\        capability-native operating system kernel",
    "       / /  \\ \\",
    "      / /____\\ \\",
    "     /__________\\",
};

static void print_banner(void)
{
    console_set_attr(VGA_ATTR(COL_LCYAN, COL_BLACK));
    for (u32 i = 0; i < ARRAY_LEN(banner); i++) {
        console_write(banner[i]);
        console_putc('\n');
    }
    console_set_attr(VGA_ATTR(COL_DGRAY, COL_BLACK));
    console_write("  every resource is an object; every right is a capability.\n\n");
    console_set_attr(VGA_ATTR(COL_LGRAY, COL_BLACK));
}

static void print_memory_map(const BootInfo *bi)
{
    const E820Entry *map = (const E820Entry *)(uintptr_t)bi->e820_addr;
    u64 usable = 0;
    u32 shown = 0;

    for (u32 i = 0; i < bi->e820_count; i++) {
        if (map[i].type == E820_USABLE) {
            usable += map[i].length;
        }
        if (shown < 8) {
            kprintf("  [mem] %016llx - %016llx  %s\n",
                    map[i].base,
                    map[i].base + map[i].length,
                    map[i].type == E820_USABLE ? "usable" : "reserved");
            shown++;
        }
    }
    if (bi->e820_count > shown) {
        kprintf("  [mem] ... %u more entries\n", bi->e820_count - shown);
    }
    kprintf("  [mem] usable RAM: %llu MiB across %u entries\n",
            usable / MB, bi->e820_count);
}

void kmain(u64 bootinfo_addr)
{
    BootInfo *bi = (BootInfo *)(uintptr_t)bootinfo_addr;

    console_init(0xB8000, 80, 25);
    print_banner();
    event_init();

    if (bi->magic != DELTA_BOOT_MAGIC || bi->version != DELTA_BOOT_VERSION) {
        kpanic("invalid boot information at %p (magic=%08x version=%u)",
               (void *)bootinfo_addr, bi->magic, bi->version);
    }

    kprintf("[boot] DELTA v%s \"%s\" kernel at %p, %llu bytes\n",
            DELTA_VERSION_STRING, DELTA_CODENAME,
            (void *)bi->kernel_base, bi->kernel_size);
    kprintf("[boot] image spans %p .. %p (including bss)\n",
            (void *)__kernel_start, (void *)__kernel_end);
    kprintf("[boot] boot flags: 0x%x\n", bi->flags);
    event_log(EVT_BOOT, bi->flags, bi->e820_count, (u64)(uintptr_t)__kernel_end);

    print_memory_map(bi);

    kprintf("\n[core] console online (VGA text + 16550 serial)\n");
    kprintf("[core] long mode active, identity map covers first 1 GiB\n");

    gdt_init();
    kprintf("[core] GDT + 64-bit TSS installed\n");

    interrupts_init();
    kprintf("[core] IDT installed; PIC remapped; PIT at %u Hz\n", TIMER_HZ);
    kprintf("[core] keyboard and serial receive IRQs online\n");
    interrupts_enable();

    u64 start = timer_ticks();
    while (timer_ticks() - start < TIMER_HZ / 2) {
        cpu_halt();
    }
    kprintf("[core] interrupt clock verified: %llu ticks in ~500 ms\n", timer_ticks());

    pmm_init(bi);
    vmm_init();

    /* Reserve a fixed, identity-mapped heap. The prototype keeps the heap in
     * the low 1 GiB physical window so the kernel can use it without a
     * higher-half direct map. */
    const u64 heap_base = 32 * MB;
    const u64 heap_size = 16 * MB;
    if (pmm_free_frames() * PAGE_SIZE < heap_size + 16 * MB) {
        kpanic("insufficient RAM for the DELTA heap (need at least 128 MiB)");
    }
    pmm_reserve(heap_base, heap_size);
    heap_init(heap_base, heap_size);

    u8 *probe = kmalloc(64);
    u8 *probe2 = kmalloc(4096);
    if (!probe || !probe2) {
        kpanic("heap allocation failed");
    }
    memset(probe, 0xA5, 64);
    memset(probe2, 0x5A, 4096);
    kprintf("[mem] heap probe at %p and %p, used=%llu KiB free=%llu KiB\n",
            probe, probe2, heap_used() / KB, heap_free() / KB);
    kfree(probe);
    kfree(probe2);

    u64 space = vmm_create_space();
    if (!space) {
        kpanic("could not create an address space");
    }
    if (!vmm_map_user_alloc(space, USER_BASE, PAGE_WRITE | PAGE_NX)) {
        kpanic("could not map a user page");
    }
    u64 user_phys = vmm_phys_of(space, USER_BASE);
    bool range = vmm_user_range_ok(space, USER_BASE, PAGE_SIZE, true);
    kprintf("[mem] user address space at %p, page %p, range_ok=%u\n",
            (void *)space, (void *)user_phys, range);
    vmm_destroy_space(space);

    kprintf("\n  DELTA is alive.\n");
    kprintf("  Memory and page tables are online; entering ring 3.\n");

    process_init();
    process_set_boot_flags(bi->flags);

    console_set_attr(VGA_ATTR(COL_BLACK, COL_LGREEN));
    kprintf("  [ OK ] kernel core complete ");
    console_set_attr(VGA_ATTR(COL_LGRAY, COL_BLACK));
    kprintf("\n\n");

    sched_start();
}
