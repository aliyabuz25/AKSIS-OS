/* One owner for the shared i8042 data port. */
#include "ps2.h"
#include "keyboard.h"
#include "mouse.h"

#define WAIT_LIMIT 1000000
static int ready;
static int wait_write(void) {
    for (unsigned i = 0; i < WAIT_LIMIT; i++) {
        uint8_t status = inb(0x64);
        if (status == 0xff) return 0;
        if (!(status & 2)) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}
static int write_port(uint16_t port, uint8_t data) {
    if (!wait_write()) return 0;
    outb(port, data);
    return 1;
}
static void dispatch(uint8_t status, uint8_t data) {
    if (status & 0xc0) return; /* parity/timeout */
    if (status & 0x20) { mouse_handle_byte(data); return; }
    kbd_handle_byte(data);
}
int ps2_init(void) {
    if (ready) return 1;
    if (!write_port(0x64, 0xad) || !write_port(0x64, 0xa7)) return 0;
    for (unsigned i = 0; i < 256 && (inb(0x64) & 1); i++) (void)inb(0x60);
    if (!write_port(0x64, 0x20)) return 0;
    uint8_t config = 0;
    unsigned i;
    for (i = 0; i < WAIT_LIMIT; i++) {
        if (inb(0x64) & 1) { config = inb(0x60); break; }
        __asm__ volatile("pause");
    }
    if (i == WAIT_LIMIT) return 0;
    /* Polling, both ports enabled, keyboard set 2 translated to set 1. */
    config = (config | 0x40) & (uint8_t)~0x33;
    ready = write_port(0x64, 0x60) && write_port(0x60, config)
        && write_port(0x64, 0xae) && write_port(0x64, 0xa8);
    return ready;
}
int ps2_device_command(int auxiliary, uint8_t command) {
    for (unsigned retry = 0; retry < 3; retry++) {
        if (auxiliary && !write_port(0x64, 0xd4)) return 0;
        if (!write_port(0x60, command)) return 0;
        for (unsigned i = 0; i < WAIT_LIMIT; i++) {
            uint8_t status = inb(0x64);
            if (status == 0xff) return 0;
            if (!(status & 1)) { __asm__ volatile("pause"); continue; }
            uint8_t data = inb(0x60);
            if (status & 0xc0) continue;
            if (data == 0xfa) return 1;
            if (data == 0xfe) break;
            if (!!(status & 0x20) == !!auxiliary) {
                /* This device is being configured: ignore stale packets. */
            } else dispatch(status, data);
        }
    }
    return 0;
}
void ps2_poll(void) {
    if (!ready) return;
    for (unsigned i = 0; i < 128; i++) {
        uint8_t status = inb(0x64);
        if (status == 0xff || !(status & 1)) break;
        dispatch(status, inb(0x60));
    }
}
