#ifndef FIRMWARE_MEMORY_H
#define FIRMWARE_MEMORY_H

/* Objects used by storage and USB middleware must be placed outside DTCM.
 * The linker owns the section placement; this header is the common contract
 * consumed by Application and Platform without exposing a Platform header. */
#if defined(__GNUC__)
#define FIRMWARE_STORAGE_RAM __attribute__((section(".storage_ram_d2"), aligned(32)))
#else
#define FIRMWARE_STORAGE_RAM
#endif

#endif
