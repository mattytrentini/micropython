/* STM32C552VE: 512 KiB flash, 128 KiB SRAM; no application bootloader. */
MEMORY
{
    FLASH (rx)     : ORIGIN = 0x08000000, LENGTH = 384K
    FLASH_FS (rx)  : ORIGIN = 0x08060000, LENGTH = 128K
    RAM (xrw)      : ORIGIN = 0x20000000, LENGTH = 120K
    FS_CACHE (xrw) : ORIGIN = 0x2001e000, LENGTH = 8K
}

_minimum_stack_size = 2K;
_minimum_heap_size = 16K;

/* Full-descending, 8-byte-aligned stack; cache is outside RAM/GC extents. */
_estack = ORIGIN(RAM) + LENGTH(RAM) - _estack_reserve;
_sstack = _estack - 16K;
_ram_start = ORIGIN(RAM);
_ram_end = ORIGIN(RAM) + LENGTH(RAM);
_heap_start = _ebss;
_heap_end = _sstack;

_micropy_hw_internal_flash_storage_ram_cache_start = ORIGIN(FS_CACHE);
_micropy_hw_internal_flash_storage_ram_cache_end = ORIGIN(FS_CACHE) + LENGTH(FS_CACHE);
_micropy_hw_internal_flash_storage_start = ORIGIN(FLASH_FS);
_micropy_hw_internal_flash_storage_end = ORIGIN(FLASH_FS) + LENGTH(FLASH_FS);

/* The ST C startup names the initial stack pointer __StackTop. */
__StackTop = _estack;
