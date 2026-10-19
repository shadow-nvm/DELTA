#include <delta/interrupts.h>
#include <delta/console.h>
#include <delta/event.h>
#include <delta/gdt.h>
#include <delta/io.h>
#include <delta/keyboard.h>
#include <delta/serial.h>
#include <delta/string.h>
#include <delta/syscall.h>
#include <delta/timer.h>
#include <delta/process.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

typedef struct PACKED {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} IdtEntry;

static IdtEntry g_idt[256];
static DescriptorPtr g_idtr;
static IrqHandler g_irq_handlers[16];

static const char *exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint",
    "overflow", "bound range", "invalid opcode", "device unavailable",
    "double fault", "coprocessor segment", "invalid TSS", "segment not present",
    "stack fault", "general protection", "page fault", "reserved",
    "x87 floating point", "alignment check", "machine check", "SIMD floating point",
    "virtualization", "control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection", "VMM communication", "security", "reserved",
};

static void idt_set_gate(u8 vector, void *handler, u8 dpl)
{
    u64 addr = (u64)(uintptr_t)handler;
    IdtEntry *e = &g_idt[vector];
    e->offset_low = (u16)(addr & 0xFFFF);
    e->selector = GDT_KCODE;
    e->ist = 0;
    e->type_attr = (u8)(0x8E | (dpl << 5));
    e->offset_mid = (u16)((addr >> 16) & 0xFFFF);
    e->offset_high = (u32)(addr >> 32);
    e->zero = 0;
}

static void idt_init(void)
{
    memset(g_idt, 0, sizeof(g_idt));
    for (u32 i = 0; i < 48; i++) {
        idt_set_gate((u8)i, (void *)(uintptr_t)isr_stub_table[i], 0);
    }
    idt_set_gate(0x80, (void *)isr_syscall, 3);

    g_idtr.limit = (u16)(sizeof(g_idt) - 1);
    g_idtr.base = (u64)(uintptr_t)&g_idt;
    __asm__ volatile("lidt %0" : : "m"(g_idtr));
}

static void pic_remap(void)
{
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();   /* master vectors 0x20..0x27 */
    outb(PIC2_DATA, 0x28); io_wait();   /* slave vectors 0x28..0x2F */
    outb(PIC1_DATA, 0x04); io_wait();   /* slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    (void)mask1;
    (void)mask2;
}

void irq_register(u8 irq, IrqHandler handler)
{
    if (irq < 16) {
        g_irq_handlers[irq] = handler;
    }
}

void irq_unmask(u8 irq)
{
    u16 port;
    u8 bit;
    if (irq < 8) {
        port = PIC1_DATA;
        bit = irq;
    } else {
        port = PIC2_DATA;
        bit = (u8)(irq - 8);
        irq_unmask(2);
    }
    outb(port, (u8)(inb(port) & ~(1u << bit)));
}

void irq_mask(u8 irq)
{
    u16 port;
    u8 bit;
    if (irq < 8) {
        port = PIC1_DATA;
        bit = irq;
    } else {
        port = PIC2_DATA;
        bit = (u8)(irq - 8);
    }
    outb(port, (u8)(inb(port) | (1u << bit)));
}

void pic_eoi(u8 irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

static void irq_dispatch(u8 irq, TrapFrame *frame)
{
    if (irq >= 16) {
        return;
    }
    /* Spurious IRQ7/IRQ15: do not send an EOI when the PIC did not raise it. */
    if (irq == 7 || irq == 15) {
        u16 cmd = (irq == 7) ? PIC1_CMD : PIC2_CMD;
        outb(cmd, 0x0B);
        u8 isr = inb(cmd);
        if (!(isr & (irq == 7 ? 0x80 : 0x80))) {
            return;
        }
    }
    if (g_irq_handlers[irq]) {
        g_irq_handlers[irq](frame);
    }
    pic_eoi(irq);
}

static u64 exception_dispatch(TrapFrame *frame)
{
    u64 cr2 = read_cr2();
    Process *p = process_current();
    bool user = (frame->cs & 3) == 3;

    kprintf("\n[exception] %s (vector %llu, error %llx)\n",
            frame->vector < 32 ? exception_names[frame->vector] : "unknown",
            frame->vector, frame->error);
    kprintf("  rip=%p cs=%llx rflags=%llx rsp=%llx cr2=%p\n",
            (void *)frame->rip, frame->cs, frame->rflags, frame->rsp, (void *)cr2);
    if (user && p) {
        kprintf("  [fault] terminating user process %s (pid %u)\n",
                p->name, p->pid);
        event_log(EVT_FAULT, p->pid, frame->vector, frame->rip);
        return process_exit_current(frame, -11);
    }
    kpanic("unhandled CPU exception in kernel context");
}

static void timer_irq(TrapFrame *frame)
{
    (void)frame;
    timer_tick();
    process_wake_sleepers();
}

u64 interrupt_dispatch(TrapFrame *frame)
{
    if (frame->vector == 0x80) {
        return syscall_dispatch(frame);
    }
    if (frame->vector < 32) {
        return exception_dispatch(frame);
    }
    if (frame->vector >= 32 && frame->vector < 48) {
        u8 irq = (u8)(frame->vector - 32);
        irq_dispatch(irq, frame);
        /* Timer preemption is only attempted at a ring-3 boundary. Kernel
         * execution (including SYS_IDLE's HLT) is never rescheduled in place,
         * which keeps trap-frame layouts unambiguous. */
        if (irq == 0 && (frame->cs & 3) == 3 && process_current() &&
            process_current()->state == PROC_RUNNING) {
            return sched_tick_current(frame);
        }
        return 0;
    }
    return 0;
}

void interrupts_init(void)
{
    memset(g_irq_handlers, 0, sizeof(g_irq_handlers));
    idt_init();
    pic_remap();

    timer_init();
    keyboard_init();
    serial_init();

    irq_register(0, timer_irq);
    irq_register(1, keyboard_irq);
    irq_register(4, serial_irq);

    irq_unmask(0);
    irq_unmask(1);
    irq_unmask(4);
    serial_enable_irq();
}
