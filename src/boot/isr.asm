; =============================================================================
;  DELTA - interrupt and exception entry stubs
;
;  Frame layout (low address to high address), matching delta/interrupts.h:
;    r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
;    vector error rip cs rflags rsp ss
; =============================================================================
BITS 64
section .text

extern interrupt_dispatch

%macro ISR_STUB 2
global isr%1
isr%1:
%if %2
    push qword %1
%else
    push qword 0
    push qword %1
%endif
    jmp isr_common
%endmacro

%assign v 0
%rep 32
    %if v = 8 || v = 10 || v = 11 || v = 12 || v = 13 || v = 14 || v = 17 || v = 21 || v = 29 || v = 30
        ISR_STUB %[v], 1
    %else
        ISR_STUB %[v], 0
    %endif
    %assign v v+1
%endrep

%assign v 32
%rep 16
    ISR_STUB %[v], 0
    %assign v v+1
%endrep

global isr_syscall
isr_syscall:
    push qword 0
    push qword 0x80
    jmp isr_common

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call interrupt_dispatch

    test rax, rax
    jz .restore
    mov rsp, rax

.restore:
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

section .rodata
align 8
global isr_stub_table
isr_stub_table:
%assign v 0
%rep 48
    dq isr%[v]
    %assign v v+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
