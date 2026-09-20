#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

/* Select the update startup flow at build time.  The default is production. */
#ifndef BOOTLOADER_UPDATE_USE_JOURNAL
#define BOOTLOADER_UPDATE_USE_JOURNAL 1U
#endif

#if (BOOTLOADER_UPDATE_USE_JOURNAL != 0U) && (BOOTLOADER_UPDATE_USE_JOURNAL != 1U)
#error "BOOTLOADER_UPDATE_USE_JOURNAL must be 0U or 1U"
#endif

#endif /* BOOTLOADER_CONFIG_H */
