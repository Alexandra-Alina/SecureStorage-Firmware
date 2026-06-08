/* Appli project: XIP from external XSPI2 NOR flash at 0x70000000 (128 MB)
   Mirrors EWARM/Appli/stm32h7rsxx_ROMxspi2.icf */

ENTRY(Reset_Handler)

_estack = ORIGIN(DTCM) + LENGTH(DTCM);
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{
    XSPI2_ROM (rx)  : ORIGIN = 0x70000000, LENGTH = 128M    /* 0x70000000–0x77FFFFFF */
    RAM       (xrw) : ORIGIN = 0x24000000, LENGTH = 0x71C00  /* 0x24000000–0x24071BFF */
    NONCACHE  (xrw) : ORIGIN = 0x24071C00, LENGTH = 1K
    DTCM      (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
    ITCM      (x)   : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >XSPI2_ROM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >XSPI2_ROM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >XSPI2_ROM

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >XSPI2_ROM
    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >XSPI2_ROM

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >XSPI2_ROM

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >XSPI2_ROM

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >XSPI2_ROM

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>XSPI2_ROM

    .noncacheable_buffer (NOLOAD) :
    {
        . = ALIGN(4);
        KEEP(*(.noncacheable_buffer))
        . = ALIGN(4);
    } >NONCACHE

    /* ETH DMA descriptor tables — MUST be in non-cacheable SRAM so the ETH DMA
     * sees consistent OWN-bit and buffer-address updates.  Cacheable AXI SRAM
     * causes a D-Cache coherency hazard: HAL_ETH_Start_IT writes descriptors
     * into cache lines; DMA reads from SRAM and sees stale zeroed values, making
     * the DMA enter Suspended/Buffer-Unavailable state indefinitely.
     * An MPU region (added in Appli main.c) marks this NONCACHE window as
     * TEX=1,C=0,B=0 (Normal, Non-cacheable). */
    .RxDescripSection (NOLOAD) :
    {
        . = ALIGN(32);
        KEEP(*(.RxDescripSection))
        . = ALIGN(32);
    } >NONCACHE

    .TxDescripSection (NOLOAD) :
    {
        . = ALIGN(32);
        KEEP(*(.TxDescripSection))
        . = ALIGN(32);
    } >NONCACHE

    /* LwIP zero-copy RX pool — must be in DMA-accessible, cache-line-aligned RAM */
    .Rx_PoolSection (NOLOAD) :
    {
        . = ALIGN(32);
        KEEP(*(.Rx_PoolSection))
        . = ALIGN(32);
    } >RAM

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >DTCM

    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
