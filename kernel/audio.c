/* Intel ICH AC'97 stereo PCM output, bounded DMA ring and polling. */
#include "audio.h"
#include "kernel_info.h"
static uint16_t mixer,bus;
static struct { uint32_t address,length; } descriptors[32] __attribute__((aligned(16)));
static int16_t pcm[32][4096] __attribute__((aligned(16)));
static unsigned head,tail,queued;
static uint64_t played;
static uint32_t read32(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static void write32(uint16_t p,uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }
static uint32_t pci(unsigned b,unsigned d,unsigned f,unsigned r) {
    write32(0xcf8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|(r&0xfc)); return read32(0xcfc);
}
static uint32_t physical(const void *p) {
    uint64_t a=(uint64_t)p-g_sysinfo.kernel_virt_base+g_sysinfo.kernel_phys_base;
    return a<0x100000000ULL?(uint32_t)a:0;
}
void audio_stop(void) {
    if(bus) {
        outb(bus+0x1b,0); outb(bus+0x1b,2);
        for(unsigned n=0;n<10000&&(inb(bus+0x1b)&2);n++) __asm__ volatile("pause");
        outw(bus+0x16,0x1c);
    }
    head=tail=queued=0;
}
void audio_volume(unsigned percent) {
    if(!mixer) return; if(percent>100) percent=100;
    unsigned attenuation=(100-percent)*31/100;
    outw(mixer+2,percent?(attenuation<<8)|attenuation:0x8000);
}
void audio_init(void) {
    for(unsigned b=0;b<256&&!bus;b++) for(unsigned d=0;d<32&&!bus;d++)
        for(unsigned f=0;f<8&&!bus;f++) if(pci(b,d,f,0)==0x24158086u) {
            uint32_t m=pci(b,d,f,0x10),v=pci(b,d,f,0x14);
            if(!(m&1)||!(v&1)) continue;
            mixer=m&0xfffc; bus=v&0xfffc;
            uint32_t cmd=pci(b,d,f,4)&0xffff;
            write32(0xcf8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|4); write32(0xcfc,cmd|5);
        }
    if(!bus||!physical(descriptors)||!physical(pcm)) { mixer=bus=0; return; }
    write32(bus+0x2c,2); /* release cold reset, interrupts remain disabled */
    for(unsigned n=0;n<1000000&&!(read32(bus+0x30)&0x100);n++) __asm__ volatile("pause");
    outw(mixer,0); outw(mixer+0x26,0); outw(mixer+0x18,0);
    audio_volume(70); audio_stop();
}
int audio_available(void) { return bus!=0; }
int audio_start(unsigned rate) {
    if(!bus||(rate!=32000&&rate!=44100&&rate!=48000)) return 0;
    audio_stop(); played=0;
    outw(mixer+0x2a,1); outw(mixer+0x2c,rate);
    if(inw(mixer+0x2c)!=rate) return 0;
    write32(bus+0x10,physical(descriptors)); return 1;
}
void audio_poll(void) {
    if(!bus||!queued) return;
    unsigned current=inb(bus+0x14)&31;
    while(queued&&head!=current) {
        played+=(descriptors[head].length&65535)/2; head=(head+1)&31; queued--;
    }
    if(queued&&(inw(bus+0x16)&1)&&inw(bus+0x18)==0) {
        played+=(descriptors[head].length&65535)/2; head=(head+1)&31; queued--;
    }
    outw(bus+0x16,0x1c);
}
int audio_write(const int16_t *samples,unsigned frames) {
    if(!bus||!frames||frames>2048) return 0;
    audio_poll(); if(queued>=31) return 0;
    if(!queued) { audio_stop(); write32(bus+0x10,physical(descriptors)); }
    kmemcpy(pcm[tail],samples,frames*4);
    descriptors[tail].address=physical(pcm[tail]); descriptors[tail].length=frames*2;
    __asm__ volatile("":::"memory");
    outb(bus+0x15,tail); tail=(tail+1)&31; queued++;
    /* Rewriting RUN on QEMU advances CIV and drops the current buffer.
       Extend LVI while running; only start the DMA engine once. */
    if(!(inb(bus+0x1b)&1)) outb(bus+0x1b,1);
    return 1;
}
uint64_t audio_frames(void) { audio_poll(); return played; }
