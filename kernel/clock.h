#ifndef WALL_CLOCK_H
#define WALL_CLOCK_H
#include "kernel.h"
typedef struct { int year, month, day, hour, minute, second, weekday; } wall_time_t;
int clock_read(wall_time_t *out, int utc_offset_minutes);
void clock_time_text(const wall_time_t *t, char *out);
void clock_date_text(const wall_time_t *t, char *out);
#endif
