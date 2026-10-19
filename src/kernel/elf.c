#include <delta/elf.h>
#include <delta/io.h>
#include <delta/paging.h>
#include <delta/pmm.h>
#include <delta/string.h>

#define ELF_MAGIC     0x464C457Fu
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define ET_EXEC       2
#define EM_X86_64     62
#define PT_LOAD       1
#define PF_X          1
#define PF_W          2
#define PF_R          4

typedef struct PACKED {
    u8  ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} Elf64Header;

typedef struct PACKED {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} Elf64Phdr;

bool elf_load_user(u64 pml4_phys, const u8 *image, usize size, u64 *entry_out)
{
    if (size < sizeof(Elf64Header)) {
        return false;
    }
    const Elf64Header *h = (const Elf64Header *)image;
    if (h->ident[0] != 0x7F || h->ident[1] != 'E' ||
        h->ident[2] != 'L' || h->ident[3] != 'F') {
        return false;
    }
    if (h->ident[4] != ELFCLASS64 || h->ident[5] != ELFDATA2LSB ||
        h->machine != EM_X86_64 || h->type != ET_EXEC) {
        return false;
    }
    if (h->phentsize < sizeof(Elf64Phdr) || h->phnum == 0 || h->phnum > 32) {
        return false;
    }
    if (h->phoff > size || (u64)h->phnum * h->phentsize > size - h->phoff) {
        return false;
    }
    if (h->entry < USER_BASE || h->entry >= USER_TOP) {
        return false;
    }

    /* First pass: validate and reserve user pages. */
    for (u32 i = 0; i < h->phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)(image + h->phoff + i * h->phentsize);
        if (ph->type != PT_LOAD) {
            continue;
        }
        if (ph->memsz == 0) {
            continue;
        }
        if (ph->filesz > ph->memsz) {
            return false;
        }
        if (ph->offset > size || ph->filesz > size - ph->offset) {
            return false;
        }
        if (ph->vaddr < USER_BASE || ph->vaddr + ph->memsz < ph->vaddr ||
            ph->vaddr + ph->memsz > USER_TOP) {
            return false;
        }
        u64 flags = PAGE_USER | PAGE_NX;
        if (ph->flags & PF_W) {
            flags |= PAGE_WRITE;
        }
        if (ph->flags & PF_X) {
            flags &= ~PAGE_NX;
        }
        for (u64 page = ALIGN_DOWN(ph->vaddr, PAGE_SIZE);
             page < ALIGN_UP(ph->vaddr + ph->memsz, PAGE_SIZE);
             page += PAGE_SIZE) {
            if (!vmm_phys_of(pml4_phys, page)) {
                if (!vmm_map_user_alloc(pml4_phys, page, flags)) {
                    return false;
                }
            }
        }
    }

    /* Second pass: copy file-backed bytes while this address space is active.
     * The kernel identity map is shared by every address space, so the switch
     * does not invalidate code or the kernel stack. */
    u64 old_cr3 = read_cr3();
    vmm_switch(pml4_phys);
    for (u32 i = 0; i < h->phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)(image + h->phoff + i * h->phentsize);
        if (ph->type != PT_LOAD || ph->filesz == 0) {
            continue;
        }
        memcpy((void *)(uintptr_t)ph->vaddr, image + ph->offset, (usize)ph->filesz);
    }
    vmm_switch(old_cr3);

    *entry_out = h->entry;
    return true;
}
