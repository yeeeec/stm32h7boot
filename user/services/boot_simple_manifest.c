#include "boot_simple_manifest.h"

#include <stdio.h>
#include <string.h>

#include "platform/boot_platform.h"

typedef struct __attribute__((packed)) {
    char magic[BOOT_VERSION_MAGIC_LENGTH];
    char project_name[BOOT_VERSION_PROJECT_NAME_LENGTH];
    char image_tag[BOOT_VERSION_IMAGE_TAG_LENGTH];
    char git_hash[BOOT_VERSION_GIT_HASH_LENGTH];
    char build_time[BOOT_BUILD_TIME_LENGTH];
    uint32_t raw_bin_crc32;
    uint32_t write_address;
    uint32_t valid_bin_size;
    uint8_t reserved[BOOT_VERSION_RESERVED_SIZE];
} BootVersionInfoHeader;

_Static_assert(sizeof(BootVersionInfoHeader) == BOOT_VERSION_INFO_SIZE,
               "Boot version header size must be 96 bytes");

static void Boot_SimpleManifest_CopyText(char *dest,
                                         size_t dest_size,
                                         const char *src,
                                         size_t src_size) {
    size_t length = 0U;

    if ((dest == NULL) || (dest_size == 0U)) {
        return;
    }

    while ((length < src_size) && (src[length] != '\0')) {
        ++length;
    }

    if (length >= dest_size) {
        length = dest_size - 1U;
    }

    memcpy(dest, src, length);
    dest[length] = '\0';
}

void Boot_SimpleManifest_Reset(BootAppImageInfo *image) {
    if (image != NULL) {
        memset(image, 0, sizeof(*image));
    }
}

BootError Boot_SimpleManifest_BuildAppPath(char *buffer, size_t buffer_length) {
    int written_length;

    if ((buffer == NULL) || (buffer_length == 0U)) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    written_length = snprintf(buffer, buffer_length, "%s", BOOT_USB_APP_IMAGE_PATH);
    if ((written_length <= 0) || ((size_t) written_length >= buffer_length)) {
        return BOOT_ERR_FILE_SIZE;
    }

    return BOOT_ERR_NONE;
}

BootError Boot_SimpleManifest_Load(BootAppImageInfo *image) {
    BootPlatformFile file;
    BootVersionInfoHeader header;
    uint32_t bytes_read = 0U;
    uint32_t file_size;
    BootError error;

    if (image == NULL) {
        return BOOT_ERR_INVALID_ARGUMENT;
    }

    Boot_SimpleManifest_Reset(image);

    error = Boot_Platform_FileOpenRead(BOOT_USB_APP_IMAGE_PATH, &file);
    if (error != BOOT_ERR_NONE) {
        return error;
    }

    file_size = Boot_Platform_FileSize(&file);
    if (file_size <= BOOT_VERSION_INFO_SIZE) {
        Boot_Platform_FileClose(&file);
        return BOOT_ERR_FILE_SIZE;
    }

    error = Boot_Platform_FileRead(&file, image->header, BOOT_VERSION_INFO_SIZE, &bytes_read);
    Boot_Platform_FileClose(&file);
    if ((error != BOOT_ERR_NONE) || (bytes_read != BOOT_VERSION_INFO_SIZE)) {
        return BOOT_ERR_FILE_HEADER;
    }

    memcpy(&header, image->header, sizeof(header));

    if (memcmp(header.magic, BOOT_VERSION_MAGIC, BOOT_VERSION_MAGIC_LENGTH) != 0) {
        return BOOT_ERR_FILE_HEADER;
    }

    if (memcmp(header.image_tag, BOOT_VERSION_IMAGE_TAG, BOOT_VERSION_IMAGE_TAG_LENGTH) != 0) {
        return BOOT_ERR_IMAGE_TYPE;
    }

    if (header.raw_bin_crc32 == 0U) {
        return BOOT_ERR_FILE_CRC;
    }

    if ((header.valid_bin_size == 0U) ||
        (header.valid_bin_size != (file_size - BOOT_VERSION_INFO_SIZE))) {
        return BOOT_ERR_FILE_SIZE;
    }

    Boot_SimpleManifest_CopyText(image->file, sizeof(image->file), BOOT_USB_APP_IMAGE_NAME,
                                 sizeof(BOOT_USB_APP_IMAGE_NAME) - 1U);
    Boot_SimpleManifest_CopyText(image->project_name, sizeof(image->project_name), header.project_name,
                                 sizeof(header.project_name));
    Boot_SimpleManifest_CopyText(image->git_hash, sizeof(image->git_hash), header.git_hash,
                                 sizeof(header.git_hash));
    Boot_SimpleManifest_CopyText(image->build_time, sizeof(image->build_time), header.build_time,
                                 sizeof(header.build_time));
    image->size          = header.valid_bin_size;
    image->crc32         = header.raw_bin_crc32;
    image->write_address = header.write_address;

    return BOOT_ERR_NONE;
}
