#ifndef FIRMWARE_BOOT_CONFIG_H
#define FIRMWARE_BOOT_CONFIG_H

/* Product policy kept in one place so that the boot flow does not contain
 * board-specific literals.  Integrators may override these values from the
 * compiler command line. */
#define BOOT_FAST_UPDATE_CHECK_ENABLE 1
#define BOOT_INSTALL_RETRY_COUNT      2U
#define BOOT_RECOVERY_RETRY_COUNT     2U
#define BOOT_SUPPORTED_FORMAT_VERSION 1U
#define BOOT_PRODUCT_NAME             "HMI"
#define BOOT_HARDWARE_NAME            "STM32H743-W25Q256"

/* Partition limits are policy values, not addresses.  Board integrations can
 * override them to match the actual QSPI/secondary-MCU layout. */
#define BOOT_APP_MAX_SIZE           (4UL * 1024UL * 1024UL)
#define BOOT_GUI_MAX_SIZE           (16UL * 1024UL * 1024UL)
#define BOOT_THERAPY_MAX_SIZE       (2UL * 1024UL * 1024UL)
#define BOOT_APP_TARGET_OFFSET      0UL
#define BOOT_GUI_TARGET_OFFSET      (4UL * 1024UL * 1024UL)
#define BOOT_THERAPY_TARGET_ADDRESS 0x08000000UL
#define BOOTLOADER_VERSION_MAJOR    1U
#define BOOTLOADER_VERSION_MINOR    0U
#define BOOTLOADER_VERSION_PATCH    0U

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
