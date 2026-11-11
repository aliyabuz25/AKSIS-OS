#ifndef SHENLONG_AUDIO_H
#define SHENLONG_AUDIO_H
#include "kernel.h"
void audio_init(void);
int audio_available(void);
int audio_start(unsigned rate);
void audio_stop(void);
void audio_poll(void);
int audio_write(const int16_t *samples,unsigned frames);
void audio_volume(unsigned percent);
uint64_t audio_frames(void);
#endif
