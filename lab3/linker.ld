ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot_header ALIGN(8) : {
        KEEP(*(.multiboot_header))
    }

    .text : {
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
