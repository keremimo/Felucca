/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MELODEE memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C06000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver). Unused RAMTEXT/debug space ends at 0x01C07F00:
 * crt0 still writes STATUS in the last 256 bytes. Linker-derived SRAM tails
 * join the audio resource banks without changing retained state or stacks.
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x83FBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .dsp_text : { _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; } > RAMTEXT AT > XIP
    _dsp_load = LOADADDR(.dsp_text);
    .dsp_tables : { _dt_start = .; *(.dsp_tables*) . = ALIGN(4); _dt_end = .; } > RAMTEXT AT > XIP
    _dt_load = LOADADDR(.dsp_tables);
    _resource_low_start = ALIGN(_dt_end, 4);
    _resource_low_end = 0x01C07F00;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = _bss_end;
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .; _resource_start = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    _resource_end = ORIGIN(POOL) + LENGTH(POOL);
    ASSERT(_resource_end - _resource_start >= 152K, "audio working arena below worst-case budget")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    _resource_retained_start = _noinit_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    /* Never part of .bss or startup zeroing: use only after HAL self-test. */
    _resource_cache_start = ORIGIN(CACHE);
    _resource_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overlaps startup STATUS")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    ASSERT(_resource_retained_start <= _resource_retained_end, "retained state overlaps boot records")
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
