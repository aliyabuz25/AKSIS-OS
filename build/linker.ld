/*
 * SHENLONG OS - Limine Boot Protocol Linker Script (KASLR uyumlu)
 * Kernel yüksek yarı adresinde: 0xFFFFFFFF80200000 base
 * KASLR ile Limine bu adresi rastgele kaydırır
 */

ENTRY(kernel_entry)

PHDRS {
    requests PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0xFFFFFFFF80200000;

    .requests_start_marker : {
        KEEP(*(.requests_start_marker))
    } :requests

    .requests : {
        KEEP(*(.requests))
    } :requests

    .requests_end_marker : {
        KEEP(*(.requests_end_marker))
    } :requests

    .text ALIGN(0x1000) : {
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x1000) : {
        *(.rodata .rodata.*)
    } :rodata

    .data ALIGN(0x1000) : {
        *(.data .data.*)
    } :data

    .bss ALIGN(0x1000) : {
        bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        bss_end = .;
    } :data

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}
