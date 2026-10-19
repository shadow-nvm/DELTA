; DELTA userspace C runtime entry.
BITS 64
section .text.entry
global _start
extern user_main

_start:
    xor rbp, rbp
    and rsp, -16
    call user_main
    mov rdi, rax
    mov eax, 1                  ; SYS_EXIT
    int 0x80
.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
