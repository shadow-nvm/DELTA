#include <delta/serial.h>
#include <delta/input.h>
#include <delta/io.h>

#define COM1 SERIAL_COM1

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* disable UART interrupts while configuring */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor 1 => 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0x07);   /* FIFO enable+clear, trigger at 1 byte */
    outb(COM1 + 4, 0x0B);   /* DTR/RTS asserted, OUT2 enables IRQ delivery */
}

void serial_enable_irq(void)
{
    (void)inb(COM1 + 5);
    (void)inb(COM1 + 0);
    outb(COM1 + 1, 0x01);   /* received-data-available interrupt */
}

void serial_putc(char c)
{
    if (c == '\n') {
        serial_putc('\r');
    }
    for (u32 spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + 5) & 0x20) {
            break;
        }
        io_wait();
    }
    outb(COM1, (u8)c);
}

bool serial_received(void)
{
    return (inb(COM1 + 5) & 1) != 0;
}

char serial_read(void)
{
    while (!serial_received()) {
    }
    return (char)inb(COM1);
}

void serial_irq(TrapFrame *frame)
{
    (void)frame;
    for (u32 i = 0; i < 32; i++) {
        u8 iir = inb(COM1 + 2);
        if (iir & 1) {
            break; /* no interrupt pending */
        }
        switch (iir & 0x0E) {
        case 0x04:                       /* received data available */
        case 0x0C:                       /* character timeout */
            input_push((char)inb(COM1));
            break;
        case 0x06:                       /* line status */
            (void)inb(COM1 + 5);
            break;
        case 0x00:                       /* modem status */
            (void)inb(COM1 + 6);
            break;
        default:                         /* transmitter holding empty */
            return;
        }
    }
}
