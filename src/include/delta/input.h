#ifndef DELTA_INPUT_H
#define DELTA_INPUT_H

#include <delta/types.h>

void input_init(void);
void input_push(char c);
int  input_pop(void);      /* -1 when empty */
u32  input_count(void);
void input_set_wake(void (*cb)(void));

#endif
