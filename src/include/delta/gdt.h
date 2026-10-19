#ifndef DELTA_GDT_H
#define DELTA_GDT_H

#include <delta/types.h>

#define GDT_KCODE  0x08
#define GDT_KDATA  0x10
#define GDT_UDATA  0x18
#define GDT_UCODE  0x20
#define GDT_TSS    0x28

#define USER_CS (GDT_UCODE | 3)
#define USER_DS (GDT_UDATA | 3)

typedef struct PACKED {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} Tss;

typedef struct PACKED {
    u16 limit;
    u64 base;
} DescriptorPtr;

void gdt_init(void);
void tss_set_rsp0(u64 rsp0);
u64  tss_get_rsp0(void);

/* Implemented in cpu.asm. */
void gdt_load(u64 descriptor_ptr_addr);

#endif
