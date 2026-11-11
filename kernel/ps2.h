#ifndef PS2_H
#define PS2_H
#include "kernel.h"
int ps2_init(void);
int ps2_device_command(int auxiliary, uint8_t command);
void ps2_poll(void);
#endif
