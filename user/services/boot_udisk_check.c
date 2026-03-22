#include "boot_udisk_check.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "platform/boot_platform.h"

#define BOOT_UDISK_VOLUME_ID_LENGTH 9U
#define BOOT_UDISK_FINGERPRINT_RAW_LENGTH                                                          \
    (4U + 4U + BOOT_PLATFORM_USB_SERIAL_LENGTH + BOOT_UDISK_VOLUME_ID_LENGTH +                     \
     sizeof(BOOT_UDISK_CHECK_SALT))

typedef BootPlatformUsbIdentity BootUdiskMediaInfo;

static uint32_t Boot_Udisk_ReadLe32(const uint8_t *data) {
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8U) | ((uint32_t) data[2] << 16U) |
           ((uint32_t) data[3] << 24U);
}

static int Boot_Udisk_StrCaseStartsWith(const char *text, const char *prefix) {
    if ((text == NULL) || (prefix == NULL)) {
        return 0;
    }

    while (*prefix != '\0') {
        if (*text == '\0') {
            return 0;
        }

        if (toupper((unsigned char) (*text)) != toupper((unsigned char) (*prefix))) {
            return 0;
        }

        ++text;
        ++prefix;
    }

    return 1;
}

static const char *Boot_Udisk_NormalizeSerialView(const char *serial) {
    if (serial == NULL) {
        return "";
    }

    if (Boot_Udisk_StrCaseStartsWith(serial, "MSFT30") != 0) {
        return serial + 6;
    }

    return serial;
}

static BootError Boot_Udisk_ReadMediaInfo(BootUdiskMediaInfo *info) {
    return Boot_Platform_ReadUsbIdentity(info);
}

static BootError Boot_Udisk_ReadFingerprintFile(uint32_t *stored_fingerprint) {
    BootPlatformFile file;
    uint32_t bytes_read;
    uint8_t file_data[BOOT_UDISK_CCK_SIZE];
    BootError error;

    if (stored_fingerprint == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    error = Boot_Platform_FileOpenRead(BOOT_UDISK_CHECK_PATH, &file);
    if (error == BOOT_ERR_FILE_MISSING) {
        return BOOT_ERR_UDISK_CODE_NOT_FOUND;
    }
    if (error != BOOT_ERR_NONE) {
        return BOOT_ERR_UDISK_CODE_PARSE;
    }

    if (Boot_Platform_FileSize(&file) != BOOT_UDISK_CCK_SIZE) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_UDISK_CODE_PARSE;
    }

    bytes_read = 0U;
    error      = Boot_Platform_FileRead(&file, file_data, sizeof(file_data), &bytes_read);
    Boot_Platform_FileClose(&file);
    if ((error != BOOT_ERR_NONE) || (bytes_read != sizeof(file_data))) {
        return BOOT_ERR_UDISK_CODE_PARSE;
    }

    *stored_fingerprint = Boot_Udisk_ReadLe32(file_data);
    return BOOT_ERR_NONE;
}

static BootError Boot_Udisk_CalcFingerprint(const BootUdiskMediaInfo *info, uint32_t *fingerprint) {
    char volume_id[BOOT_UDISK_VOLUME_ID_LENGTH];
    char raw_text[BOOT_UDISK_FINGERPRINT_RAW_LENGTH];
    int length;

    if ((info == NULL) || (fingerprint == NULL)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    (void) snprintf(volume_id, sizeof(volume_id), "%08X", (unsigned int) info->volume_id);

    length = snprintf(raw_text, sizeof(raw_text), "%s%s%s%s%s", info->usb_vid, info->usb_pid,
                      Boot_Udisk_NormalizeSerialView(info->usb_serial), volume_id,
                      BOOT_UDISK_CHECK_SALT);
    if ((length <= 0) || ((size_t) length >= sizeof(raw_text))) {
        return BOOT_ERR_UDISK_INFO_MISMATCH;
    }

    *fingerprint = Boot_Crc32_IsoCalc(raw_text, (size_t) length);
    return BOOT_ERR_NONE;
}

BootError Boot_Udisk_Check(void) {
    BootUdiskMediaInfo info;
    uint32_t calculated_fingerprint;
    uint32_t stored_fingerprint;
    BootError error;

    error = Boot_Udisk_ReadMediaInfo(&info);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_Udisk_ReadFingerprintFile(&stored_fingerprint);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    error = Boot_Udisk_CalcFingerprint(&info, &calculated_fingerprint);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    if (calculated_fingerprint != stored_fingerprint) {
        return BOOT_ERR_UDISK_FINGERPRINT_MISMATCH;
    }

    return BOOT_ERR_NONE;
}
