/*
 * SHENLONG OS - IDT (64-bit Long Mode)
 */

#include "kernel.h"
#include "fb.h"

#define IDT_ENTRIES 256

/* 64-bit IDT gate: 16 byte */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;           /* interrupt stack table (0 = none) */
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry64_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr64_t;

static idt_entry64_t idt[IDT_ENTRIES];
static idt_ptr64_t   idt_ptr;

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[num].offset_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}

/* İstisna işleyiciler */
static void exc_handler(void) {
    fb_set_fg(FB_WHITE); fb_set_bg(FB_RED);
    fb_puts("\n[EXCEPTION] CPU istisnasi!\n");
    fb_set_fg(FB_LIGHT_GREY); fb_set_bg(FB_BLACK);
    for (;;) __asm__ volatile("hlt");
}

static void gpf_handler(void) {
    fb_set_fg(FB_WHITE); fb_set_bg(FB_RED);
    fb_puts("\n[#GP] General Protection Fault!\n");
    fb_set_fg(FB_LIGHT_GREY); fb_set_bg(FB_BLACK);
    for (;;) __asm__ volatile("hlt");
}

static void pf_handler(void) {
    fb_set_fg(FB_WHITE); fb_set_bg(FB_RED);
    fb_puts("\n[#PF] Page Fault!\n");
    fb_set_fg(FB_LIGHT_GREY); fb_set_bg(FB_BLACK);
    for (;;) __asm__ volatile("hlt");
}

static void irq_default(void) {
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void idt_init(void) {
    kmemset(idt, 0, sizeof(idt));

    /* Limine 64-bit Long Mode Code Segment (CS) seçicisini dinamik al */
    uint16_t cs = 0x28;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    /* CPU istisnaları (0-31) */
    for (int i = 0; i < 32; i++)
        idt_set_gate((uint8_t)i, (uint64_t)exc_handler, cs, 0x8E);

    idt_set_gate(0x0D, (uint64_t)gpf_handler, cs, 0x8E);  /* #GP */
    idt_set_gate(0x0E, (uint64_t)pf_handler,  cs, 0x8E);  /* #PF */

    /* IRQ'lar (0x20-0x2F) */
    for (int i = 0x20; i < 0x30; i++)
        idt_set_gate((uint8_t)i, (uint64_t)irq_default, cs, 0x8E);

    /* PIC yeniden programla */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    /* Shell klavyeyi polling ile okur; hatalı bir IRQ dönüşü shell'i bozmasın. */
    outb(0x21, 0xFF); /* tüm master IRQ'lar maskeli */
    outb(0xA1, 0xFF);

    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base  = (uint64_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    /* v0.2 shell polling kullanır; IRQ'lar bilinçli olarak kapalı kalır. */
}
