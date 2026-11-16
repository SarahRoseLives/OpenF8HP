ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08001000, LENGTH = 0x1F000
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x8000
}

_estack = 0x200078E0;

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : ALIGN(4) {
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT> FLASH
    _sidata = LOADADDR(.data);

    .bss : ALIGN(4) {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.comment*)
    }
}
