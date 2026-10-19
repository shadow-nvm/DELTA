#ifndef DELTA_TIMER_H
#define DELTA_TIMER_H

#include <delta/types.h>

#define TIMER_HZ 100

void timer_init(void);
u64  timer_ticks(void);
u64  timer_ms(void);
void timer_tick(void);

#endif
