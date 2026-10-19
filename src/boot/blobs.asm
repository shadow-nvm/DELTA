; Embedded DELTA userspace.  These are complete static ELF64 images; the
; kernel's ELF loader maps them into private address spaces.
BITS 64
section .rodata

global init_elf_start
global init_elf_end
global worker_elf_start
global worker_elf_end
global spinner_elf_start
global spinner_elf_end
global idle_elf_start
global idle_elf_end

align 16
init_elf_start:
    incbin "build/user/init.elf"
init_elf_end:

align 16
worker_elf_start:
    incbin "build/user/worker.elf"
worker_elf_end:

align 16
spinner_elf_start:
    incbin "build/user/spinner.elf"
spinner_elf_end:

align 16
idle_elf_start:
    incbin "build/user/idle.elf"
idle_elf_end:

section .note.GNU-stack noalloc noexec nowrite progbits
