#ifndef FIRMWARE_IMAGE_INSTALLER_H
#define FIRMWARE_IMAGE_INSTALLER_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_INSTALLER_BLOCK_SIZE 4096U

typedef struct {
    const char *source_path;
    uint32_t target_address;
    uint32_t target_max_size;
    uint32_t expected_size;
    uint8_t expected_sha256[32];
} ImageInstallPlan_t;

typedef firmware_status_t (*image_source_open_fn)(void *context, const char *path,
                                                  uint32_t *handle, uint32_t *size);
typedef firmware_status_t (*image_source_read_fn)(void *context, uint32_t handle,
                                                  uint32_t offset, void *buffer, size_t size,
                                                  size_t *read_size);
typedef firmware_status_t (*image_source_close_fn)(void *context, uint32_t handle);
typedef firmware_status_t (*image_target_erase_fn)(void *context, uint32_t address, uint32_t size);
typedef firmware_status_t (*image_target_write_fn)(void *context, uint32_t address,
                                                   const void *data, size_t size);
typedef firmware_status_t (*image_target_read_fn)(void *context, uint32_t address,
                                                  void *data, size_t size);

typedef struct {
    image_source_open_fn source_open;
    image_source_read_fn source_read;
    image_source_close_fn source_close;
    image_target_erase_fn target_erase;
    image_target_write_fn target_write;
    image_target_read_fn target_read;
    void *context;
} image_installer_port_t;

typedef enum {
    IMAGE_INSTALL_OK = 0,
    IMAGE_INSTALL_INVALID_ARGUMENT,
    IMAGE_INSTALL_SOURCE,
    IMAGE_INSTALL_SIZE,
    IMAGE_INSTALL_ERASE,
    IMAGE_INSTALL_WRITE,
    IMAGE_INSTALL_READBACK,
    IMAGE_INSTALL_HASH,
    IMAGE_INSTALL_IO
} ImageInstallResult_t;

void ImageInstaller_SetPort(const image_installer_port_t *port);
ImageInstallResult_t ImageInstaller_Install(const ImageInstallPlan_t *plan);
ImageInstallResult_t ImageInstaller_InstallWithPort(const ImageInstallPlan_t *plan,
                                                    const image_installer_port_t *port);
const char *ImageInstaller_ResultString(ImageInstallResult_t result);

#ifdef __cplusplus
}
#endif

#endif
