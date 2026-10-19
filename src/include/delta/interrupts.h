#ifndef DELTA_INTERRUPTS_H
#define DELTA_INTERRUPTS_H

#include <delta/types.h>

typedef struct PACKED {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rbp;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 vector;
    u64 error;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} TrapFrame;

typedef void (*IrqHandler)(TrapFrame *frame);

void interrupts_init(void);
void irq_register(u8 irq, IrqHandler handler);
void irq_unmask(u8 irq);
void irq_mask(u8 irq);
void pic_eoi(u8 irq);

u64 interrupt_dispatch(TrapFrame *frame);

/* ISR entry points. */
extern u64 isr_stub_table[48];
extern void isr_syscall(void);

#endif
