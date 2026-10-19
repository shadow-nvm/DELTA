#ifndef DELTA_KEYBOARD_H
#define DELTA_KEYBOARD_H

#include <delta/interrupts.h>

void keyboard_init(void);
void keyboard_irq(TrapFrame *frame);

#endif
