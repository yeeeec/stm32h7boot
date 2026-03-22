#include "boot_usb.h"

#include "boot_config.h"
#include "platform/boot_platform.h"
#include "boot_udisk_check.h"

static uint32_t g_boot_usb_start_tick;
static uint8_t g_boot_usb_scan_started;
static uint8_t g_boot_usb_mounted;

void Boot_Usb_Init(void) {
    g_boot_usb_start_tick   = Boot_Platform_GetTickMs();
    g_boot_usb_scan_started = 0U;
    g_boot_usb_mounted      = 0U;
}

void Boot_Usb_Reset(void) {
    if (g_boot_usb_mounted != 0U) {
        Boot_Platform_UsbUnmount();
    }
    g_boot_usb_scan_started = 0U;
    g_boot_usb_mounted      = 0U;
}

bool Boot_Usb_IsMounted(void) {
    return (g_boot_usb_mounted != 0U);
}

BootError Boot_Usb_BuildPath(const char *relative_path, char *buffer, size_t buffer_length) {
    return Boot_Platform_BuildUsbPath(relative_path, buffer, buffer_length);
}

BootUsbScanResult Boot_Usb_PollForUpgradeMedia(uint32_t window_ms, BootError *error) {
    uint32_t elapsed_ms;
    BootPlatformUsbState usb_state;

    if (error != NULL) {
        *error = BOOT_ERR_NONE;
    }

    usb_state = Boot_Platform_GetUsbState();
    if (usb_state == BOOT_PLATFORM_USB_STATE_DISCONNECTED) {
        Boot_Usb_Reset();
        if (error != NULL) {
            *error = BOOT_ERR_USB_NOT_DETECTED;
        }
        return BOOT_USB_SCAN_NO_DEVICE;
    }

    if (g_boot_usb_scan_started == 0U) {
        g_boot_usb_start_tick   = Boot_Platform_GetTickMs();
        g_boot_usb_scan_started = 1U;
    }

    elapsed_ms = Boot_Platform_GetTickMs() - g_boot_usb_start_tick;
    if (usb_state != BOOT_PLATFORM_USB_STATE_READY) {
        if (elapsed_ms < window_ms) {
            return BOOT_USB_SCAN_WAITING;
        }

        if (error != NULL) {
            *error = BOOT_ERR_USB_NOT_DETECTED;
        }
        return BOOT_USB_SCAN_NO_DEVICE;
    }

    if (g_boot_usb_mounted == 0U) {
        if (Boot_Platform_UsbMount() != BOOT_ERR_NONE) {
            if (error != NULL) {
                *error = BOOT_ERR_FS_MOUNT;
            }
            return BOOT_USB_SCAN_ERROR;
        }
        g_boot_usb_mounted = 1U;
    }

#if (BOOT_UDISK_CHECK_ENABLE == 1U)
    {
        BootError udisk_error;

        udisk_error = Boot_Udisk_Check();
        if (udisk_error != BOOT_ERR_NONE) {
            if (error != NULL) {
                *error = udisk_error;
            }
            return BOOT_USB_SCAN_MEDIA_INVALID;
        }
    }
#endif

    if (Boot_Platform_FileExists(BOOT_USB_BOOT_DIR) == false) {
        if (error != NULL) {
            *error = BOOT_ERR_UPGRADE_DIR_MISSING;
        }
        return BOOT_USB_SCAN_MEDIA_INVALID;
    }

    if (Boot_Platform_FileExists(BOOT_USB_APP_IMAGE_PATH) == false) {
        if (error != NULL) {
            *error = BOOT_ERR_FILE_MISSING;
        }
        return BOOT_USB_SCAN_MEDIA_INVALID;
    }

    return BOOT_USB_SCAN_UPGRADE_READY;
}
