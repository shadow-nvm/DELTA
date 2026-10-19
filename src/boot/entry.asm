; =============================================================================
;  DELTA - 64-bit kernel entry
;  The boot loader has already installed identity page tables for the first
;  1 GiB and placed the boot information block at 0xB000.
; =============================================================================
BITS 64
section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli
    cld
    mov rsp, 0x9F000
    xor rbp, rbp

    ; A flat binary does not carry .bss. Zero it before any C code runs.
    lea rdi, [rel __bss_start]
    mov rcx, __bss_end
    sub rcx, rdi
    xor eax, eax
    rep stosb

    mov rdi, 0xB000
    call kmain

.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
