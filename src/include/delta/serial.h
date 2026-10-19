#ifndef DELTA_SERIAL_H
#define DELTA_SERIAL_H

#include <delta/interrupts.h>

#define SERIAL_COM1 0x3F8

void serial_init(void);
void serial_enable_irq(void);
void serial_putc(char c);
bool serial_received(void);
char serial_read(void);
void serial_irq(TrapFrame *frame);

#endif
