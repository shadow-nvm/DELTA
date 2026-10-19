#ifndef DELTA_BOOTINFO_H
#define DELTA_BOOTINFO_H

#include <delta/types.h>

#define DELTA_BOOT_MAGIC  0x41544C44u  /* "DLTA" */
#define DELTA_BOOT_VERSION 1u

#define DELTA_BOOT_FLAG_SELFTEST (1u << 0)

typedef struct PACKED {
    u32 magic;
    u32 version;
    u32 flags;
    u32 e820_count;
    u64 e820_addr;
    u64 kernel_base;
    u64 kernel_size;
    u64 vga_addr;
    u32 vga_cols;
    u32 vga_rows;
} BootInfo;

typedef struct PACKED {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi_ext;
} E820Entry;

#define E820_USABLE 1

#endif /* DELTA_BOOTINFO_H */
