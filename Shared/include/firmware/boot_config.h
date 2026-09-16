#ifndef FIRMWARE_BOOT_CONFIG_H
#define FIRMWARE_BOOT_CONFIG_H

/* Product policy kept in one place so that the boot flow does not contain
 * board-specific literals.  Integrators may override these values from the
 * compiler command line. */
#ifndef BOOT_FAST_UPDATE_CHECK_ENABLE
#define BOOT_FAST_UPDATE_CHECK_ENABLE 1
#endif

#ifndef BOOT_INSTALL_RETRY_COUNT
#define BOOT_INSTALL_RETRY_COUNT 2U
#endif

#ifndef BOOT_RECOVERY_RETRY_COUNT
#define BOOT_RECOVERY_RETRY_COUNT 2U
#endif

#ifndef BOOT_SUPPORTED_FORMAT_VERSION
#define BOOT_SUPPORTED_FORMAT_VERSION 1U
#endif

#ifndef BOOT_PRODUCT_NAME
#define BOOT_PRODUCT_NAME "HMI"
#endif

#ifndef BOOT_HARDWARE_NAME
#define BOOT_HARDWARE_NAME "STM32H743-W25Q256"
#endif

/* Partition limits are policy values, not addresses.  Board integrations can
 * override them to match the actual QSPI/secondary-MCU layout. */
#ifndef BOOT_APP_MAX_SIZE
#define BOOT_APP_MAX_SIZE (4UL * 1024UL * 1024UL)
#endif
#ifndef BOOT_GUI_MAX_SIZE
#define BOOT_GUI_MAX_SIZE (16UL * 1024UL * 1024UL)
#endif
#ifndef BOOT_THERAPY_MAX_SIZE
#define BOOT_THERAPY_MAX_SIZE (2UL * 1024UL * 1024UL)
#endif

#ifndef BOOTLOADER_VERSION_MAJOR
#define BOOTLOADER_VERSION_MAJOR 1U
#endif
#ifndef BOOTLOADER_VERSION_MINOR
#define BOOTLOADER_VERSION_MINOR 0U
#endif
#ifndef BOOTLOADER_VERSION_PATCH
#define BOOTLOADER_VERSION_PATCH 0U
#endif

#define BOOT_UPDATE_ROOT          "/UPDATE"
#define BOOT_UPDATE_FIRMWARE      "/UPDATE/firmware"
#define BOOT_CURRENT_ROOT         "/CURRENT"
#define BOOT_CURRENT_FIRMWARE     "/CURRENT/firmware"
#define BOOT_CURRENT_NEW_ROOT     "/CURRENT_NEW"
#define BOOT_CURRENT_NEW_FIRMWARE "/CURRENT_NEW/firmware"
#define BOOT_REQUEST_PATH         "/UPDATE/boot_update_request.json"
#define BOOT_MANIFEST_FILE        "manifest.json"
#define BOOT_APP_FILE             "hmi.app.bin"
#define BOOT_GUI_FILE             "hmi.gui.bin"
#define BOOT_THERAPY_FILE         "therapy.app.bin"

#endif
