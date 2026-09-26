#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

/* Debug owns the initial update request and confirms it after installation. */
#ifndef BOOTLOADER_UPDATE_DEBUG_MODE
#define BOOTLOADER_UPDATE_DEBUG_MODE 0U
#endif

#if (BOOTLOADER_UPDATE_DEBUG_MODE != 0U) && (BOOTLOADER_UPDATE_DEBUG_MODE != 1U)
#error "BOOTLOADER_UPDATE_DEBUG_MODE must be 0U or 1U"
#endif

#endif /* BOOTLOADER_CONFIG_H */
