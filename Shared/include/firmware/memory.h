#ifndef FIRMWARE_MEMORY_H
#define FIRMWARE_MEMORY_H

/* DMA/FatFs buffers must not be placed in DTCM on STM32H7. The linker owns
 * the section placement; this macro only expresses the cross-project ABI. */
#if defined(__GNUC__)
#define FIRMWARE_STORAGE_RAM __attribute__((section(".storage_ram_d2"), aligned(32)))
#else
#define FIRMWARE_STORAGE_RAM
#endif

#endif
