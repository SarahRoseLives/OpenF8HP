ENTRY(Reset_Handler)
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20004000, LENGTH = 0x3000
}
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
    } > RAM
    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
