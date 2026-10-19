; =============================================================================
;  DELTA - low-level CPU transitions used by the C core
; =============================================================================
BITS 64
section .text

global gdt_load
global delta_enter_user

; void gdt_load(u64 descriptor_ptr_addr)
; Load a new GDT and reload CS through a far return.  DS/ES/SS use the flat
; kernel data selector.  FS/GS are zeroed because DELTA does not use TLS in
; ring 0.
gdt_load:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    push qword 0x08
    lea rax, [rel .next]
    push rax
    retfq
.next:
    ret

; void delta_enter_user(TrapFrame *frame)
; Restore a complete ring-3 trap frame and iretq into it.  This is used for the
; first scheduling decision; after that, interrupt/syscall returns do the same
; work from the common stub.
delta_enter_user:
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
