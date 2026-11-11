/*
 * SHENLONG OS - PS/2 Klavye (Polling)
 * IRQ yerine polling: UEFI + BIOS her ikisinde çalışır
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel.h"

void    kbd_set_efi_st(void *st);  /* UEFI System Table pointer'ı ver */
void    kbd_init(void);
void    kbd_handle_byte(uint8_t sc);
#define KBD_UP    0x11
#define KBD_DOWN  0x12
#define KBD_LEFT  0x13
#define KBD_RIGHT 0x14
#define KBD_HOME  0x15
#define KBD_END   0x16
#define KBD_DEL   0x17
#define KBD_PGUP  0x18
#define KBD_PGDN  0x19
#define KBD_TAB   '\t'
#define KBD_START 0x1c
#define KBD_ESCAPE 0x1b
#define KBD_F11   0x1d
#define KBD_F12   0x1a
void    kbd_irq_handler(void);   /* IRQ1 tarafından çağrılır */
char    kbd_getc(void);     /* bloklar - karakter gelene kadar bekler */
int     kbd_poll(void);     /* 0 = yok, 1 = karakter var */

#endif
