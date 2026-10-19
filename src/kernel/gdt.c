#include <delta/gdt.h>
#include <delta/string.h>

static u64 g_gdt[7];
static DescriptorPtr g_gdtr;
static Tss g_tss;

static u64 make_tss_descriptor(u64 base, u32 limit)
{
    u64 low = 0;
    low |= (u64)(limit & 0xFFFF);
    low |= (base & 0xFFFFFFULL) << 16;
    low |= 0x89ULL << 40;                    /* present, type = available TSS */
    low |= ((u64)(limit >> 16) & 0xF) << 48;
    low |= ((base >> 24) & 0xFFULL) << 56;
    return low;
}

void gdt_init(void)
{
    memset(g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    g_gdt[1] = 0x00AF9A000000FFFFULL;        /* kernel code, DPL 0 */
    g_gdt[2] = 0x00CF92000000FFFFULL;        /* kernel data, DPL 0 */
    g_gdt[3] = 0x00CFF2000000FFFFULL;        /* user data, DPL 3 */
    g_gdt[4] = 0x00AFFA000000FFFFULL;        /* user code, DPL 3 */

    g_tss.iomap_base = (u16)sizeof(Tss);
    g_tss.rsp0 = 0x9F000;

    u64 base = (u64)(uintptr_t)&g_tss;
    u32 limit = (u32)(sizeof(Tss) - 1);
    g_gdt[5] = make_tss_descriptor(base, limit);
    g_gdt[6] = base >> 32;

    g_gdtr.limit = (u16)(sizeof(g_gdt) - 1);
    g_gdtr.base = (u64)(uintptr_t)&g_gdt;
    gdt_load((u64)(uintptr_t)&g_gdtr);

    __asm__ volatile("mov $0x28, %%ax; ltr %%ax" : : : "rax");
}

void tss_set_rsp0(u64 rsp0)
{
    g_tss.rsp0 = rsp0;
}

u64 tss_get_rsp0(void)
{
    return g_tss.rsp0;
}
