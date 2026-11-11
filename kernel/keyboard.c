/*
 * SHENLONG OS - PS/2 Klavye Polling
 * Basit ve güvenilir
 */

#include "keyboard.h"
#include "kernel.h"
#include "ps2.h"

#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define KBD_OBF     0x01
#define KBD_IBF     0x02
#define KBD_CMD     0x64

/* Turkish Q. Unicode output is queued as complete UTF-8 sequences. */
static const uint16_t sc_lo[] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','*','-','\b',
    '\t','q','w','e','r','t','y','u',0x131,'o','p',0x11f,0xfc,'\n',
    0,  'a','s','d','f','g','h','j','k','l',0x15f,'i','"',
    0,  ',','z','x','c','v','b','n','m',0xf6,0xe7,'.',
    0,  '*', 0,  ' '
};
static const uint16_t sc_hi[] = {
    0,   0,  '!','\'','^','+','%','&','/','(',')','=','?','_','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P',0x11e,0xdc,'\n',
    0,  'A','S','D','F','G','H','J','K','L',0x15e,0x130,0xe9,
    0,  ';','Z','X','C','V','B','N','M',0xd6,0xc7,':',
    0,  '*', 0,  ' '
};
static const uint16_t sc_altgr[128] = {
    [0x03]=0xa3,[0x04]='#',[0x05]='$',[0x07]=0xac,[0x08]='{',[0x09]='[',
    [0x0a]=']',[0x0b]='}',[0x0c]='\\',[0x0d]='|',[0x10]='@',[0x12]=0x20ac,
    [0x14]=0x20ba,[0x1b]='~',[0x56]='|'
};

static int shift = 0, caps = 0, ext = 0, caps_down = 0, pause_skip = 0, ctrl = 0, altgr=0;

#define BUF_SZ 128
static char kbuf[BUF_SZ];
static volatile int khead = 0, ktail = 0;

static void kpush(char c) {
    int n = (khead+1)%BUF_SZ;
    if (n != ktail) { kbuf[khead]=c; khead=n; }
}
static char kpop(void) {
    char c = kbuf[ktail]; ktail=(ktail+1)%BUF_SZ; return c;
}
static void kpush_unicode(unsigned c) {
    unsigned need=c<128?1:c<2048?2:3;
    if((unsigned)((ktail-khead-1+BUF_SZ)%BUF_SZ)<need) return;
    if(need==1) kpush(c);
    else if(need==2) { kpush(0xc0|(c>>6)); kpush(0x80|(c&63)); }
    else { kpush(0xe0|(c>>12)); kpush(0x80|((c>>6)&63)); kpush(0x80|(c&63)); }
}
static int kempty(void) { return khead==ktail; }

void kbd_set_efi_st(void *st) { (void)st; } /* artık kullanılmıyor */

void kbd_init(void) {
    shift=caps=ext=caps_down=pause_skip=khead=ktail=ctrl=altgr=0;
    ps2_device_command(0, 0xf5);
    ps2_device_command(0, 0xf0);
    ps2_device_command(0, 2);
    ps2_device_command(0, 0xf4);
}

void kbd_handle_byte(uint8_t sc) {
    if (pause_skip) { pause_skip--; return; }
    if (sc == 0xe1) { pause_skip = 5; return; }
    if (sc == 0xe0) { ext = 1; return; }
    if (sc == 0xfa || sc == 0xfe) return;
    if (ext) {
        ext = 0;
        if ((sc&0x7f)==0x1d) { if(sc&0x80) ctrl&=~2; else ctrl|=2; return; }
        if ((sc&0x7f)==0x38) { altgr=!(sc&0x80); return; }
        if (sc == 0x5b || sc == 0x5c) kpush(KBD_START);
        if (sc == 0x48) kpush(KBD_UP);
        if (sc == 0x50) kpush(KBD_DOWN);
        if (sc == 0x4b) kpush(KBD_LEFT);
        if (sc == 0x4d) kpush(KBD_RIGHT);
        if (sc == 0x47) kpush(KBD_HOME);
        if (sc == 0x4f) kpush(KBD_END);
        if (sc == 0x53) kpush(KBD_DEL);
        if (sc == 0x49) kpush(KBD_PGUP);
        if (sc == 0x51) kpush(KBD_PGDN);
        if (sc == 0x1c) kpush('\n');
        return;
    }
    uint8_t key = sc & 0x7f;
    int release = sc & 0x80;
    if (key==0x1d) { if(release) ctrl&=~1; else ctrl|=1; return; }
    if (key == 0x2a || key == 0x36) {
        int bit = key == 0x2a ? 1 : 2;
        if (release) shift &= ~bit; else shift |= bit;
        return;
    }
    if (key == 0x3a) {
        if (!release && !caps_down) caps ^= 1;
        caps_down = !release;
        return;
    }
    if (release) return;
    if (key == 0x57) { kpush(KBD_F11); return; }
    if (key == 0x58) { kpush(KBD_F12); return; }
    if (key == 0x01) { kpush(KBD_ESCAPE); return; }
    if(altgr&&sc_altgr[key]) { kpush_unicode(sc_altgr[key]); return; }
    if(key==0x56) { kpush(shift?'>':'<'); return; }
    if (key < sizeof(sc_lo)/sizeof(sc_lo[0]) && sc_lo[key]) {
        unsigned base=sc_lo[key];
        int letter=(base>='a'&&base<='z')||base==0x131||base==0x11f||base==0xfc||base==0x15f||base==0xf6||base==0xe7;
        unsigned c = (letter?(!!shift^caps):!!shift) ? sc_hi[key] : base;
        if(ctrl&&(c=='l'||c=='L')) c=12;
        else if(ctrl&&(c=='a'||c=='A')) c=1;
        kpush_unicode(c);
    }
}

void kbd_irq_handler(void) { ps2_poll(); }

int  kbd_poll(void) { ps2_poll(); return !kempty(); }
char kbd_getc(void) {
    while (1) {
        /* Klavye IRQ'sı kullanılmıyor; byte'ları güvenli biçimde burada oku. */
        ps2_poll();
        if (!kempty()) return kpop();
        __asm__ volatile("pause");
    }
}
