#ifndef DELTA_ELF_H
#define DELTA_ELF_H

#include <delta/types.h>

bool elf_load_user(u64 pml4_phys, const u8 *image, usize size, u64 *entry_out);

#endif
