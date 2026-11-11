#include "../kernel/keyboard.h"
extern int printf(const char *,...);
void ps2_poll(void) {}
int ps2_device_command(int auxiliary,uint8_t command) { (void)auxiliary; (void)command; return 1; }
static void tap(unsigned scan) { kbd_handle_byte(scan); kbd_handle_byte(scan|0x80); }
static int expect(const char *text) {
    for(unsigned i=0;text[i];i++) if(!kbd_poll()||(uint8_t)kbd_getc()!=(uint8_t)text[i]) return 0;
    return !kbd_poll();
}
#define CHECK(expr) do { if(!(expr)) { printf("FAIL keyboard line %d\n",__LINE__); return 1; } } while(0)
int main(void) {
    kbd_init();
    unsigned letters[]={0x17,0x28,0x1a,0x27,0x34,0x33,0x1b};
    for(unsigned i=0;i<7;i++) tap(letters[i]); CHECK(expect("ıiğşçöü"));
    kbd_handle_byte(0x2a); for(unsigned i=0;i<7;i++) tap(letters[i]); kbd_handle_byte(0xaa);
    CHECK(expect("IİĞŞÇÖÜ"));
    tap(0x3a); for(unsigned i=0;i<7;i++) tap(letters[i]); CHECK(expect("IİĞŞÇÖÜ"));
    kbd_handle_byte(0x36); for(unsigned i=0;i<7;i++) tap(letters[i]); kbd_handle_byte(0xb6);
    CHECK(expect("ıiğşçöü")); tap(0x3a);
    tap(0x0c); tap(0x0d); tap(0x35); tap(0x2b); CHECK(expect("*-.,"));
    kbd_handle_byte(0x2a); tap(0x08); tap(0x35); tap(0x0c); tap(0x0d); kbd_handle_byte(0xaa); CHECK(expect("/:?_"));
    kbd_handle_byte(0xe0); kbd_handle_byte(0x38); tap(0x10); tap(0x08); tap(0x0c); tap(0x12);
    kbd_handle_byte(0xe0); kbd_handle_byte(0xb8); CHECK(expect("@{\\€"));
    kbd_handle_byte(0x1d); tap(0x26); tap(0x1e); kbd_handle_byte(0x9d); CHECK(expect("\x0c\x01"));
    printf("PASS keyboard: Turkish Q, dotted/dotless I, Shift/Caps combinations, punctuation, AltGr, shortcuts.\n"); return 0;
}
