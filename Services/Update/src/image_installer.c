#include "update/image_installer.h"

#include <string.h>

#include "crypto/sha256.h"

static image_installer_port_t s_port;

void ImageInstaller_SetPort(const image_installer_port_t *port)
{
    if (port == NULL)
        (void) memset(&s_port, 0, sizeof(s_port));
    else
        s_port = *port;
}

static int valid_port(const image_installer_port_t *p)
{
    return p != NULL && p->source_open != NULL && p->source_read != NULL &&
           p->source_close != NULL && p->target_erase != NULL && p->target_write != NULL &&
           p->target_read != NULL;
}

ImageInstallResult_t ImageInstaller_InstallWithPort(const ImageInstallPlan_t *plan,
                                                    const image_installer_port_t *port)
{
    uint32_t handle = 0U, actual_size = 0U, offset = 0U;
    uint8_t source[IMAGE_INSTALLER_BLOCK_SIZE];
    uint8_t verify[IMAGE_INSTALLER_BLOCK_SIZE];
    uint8_t digest[32];
    crypto_sha256_context_t hash;
    firmware_status_t status;
    int opened = 0;

    if (plan == NULL || plan->source_path == NULL || !valid_port(port) ||
        plan->expected_size == 0U || plan->expected_size > plan->target_max_size ||
        plan->target_address > UINT32_MAX - plan->expected_size)
        return IMAGE_INSTALL_INVALID_ARGUMENT;
    status = port->source_open(port->context, plan->source_path, &handle, &actual_size);
    if (FirmwareStatus_IsError(status))
        return IMAGE_INSTALL_SOURCE;
    opened = 1;
    if (actual_size != plan->expected_size)
    {
        (void) port->source_close(port->context, handle);
        return IMAGE_INSTALL_SIZE;
    }
    status = port->target_erase(port->context, plan->target_address, plan->expected_size);
    if (FirmwareStatus_IsError(status))
    {
        (void) port->source_close(port->context, handle);
        return IMAGE_INSTALL_ERASE;
    }
    status = Crypto_Sha256Init(&hash);
    if (FirmwareStatus_IsError(status))
    {
        (void) port->source_close(port->context, handle);
        return IMAGE_INSTALL_IO;
    }
    while (offset < plan->expected_size)
    {
        const size_t requested = (plan->expected_size - offset) < sizeof(source)
                                     ? (size_t) (plan->expected_size - offset)
                                     : sizeof(source);
        size_t received        = 0U;
        status = port->source_read(port->context, handle, offset, source, requested, &received);
        if (FirmwareStatus_IsError(status) || received != requested)
        {
            Crypto_Sha256Abort(&hash);
            (void) port->source_close(port->context, handle);
            return IMAGE_INSTALL_SOURCE;
        }
        status = Crypto_Sha256Update(&hash, source, received);
        if (FirmwareStatus_IsError(status))
        {
            Crypto_Sha256Abort(&hash);
            (void) port->source_close(port->context, handle);
            return IMAGE_INSTALL_IO;
        }
        status = port->target_write(port->context, plan->target_address + offset, source, received);
        if (FirmwareStatus_IsError(status))
        {
            Crypto_Sha256Abort(&hash);
            (void) port->source_close(port->context, handle);
            return IMAGE_INSTALL_WRITE;
        }
        status = port->target_read(port->context, plan->target_address + offset, verify, received);
        if (FirmwareStatus_IsError(status))
        {
            Crypto_Sha256Abort(&hash);
            (void) port->source_close(port->context, handle);
            return IMAGE_INSTALL_READBACK;
        }
        if (memcmp(source, verify, received) != 0)
        {
            Crypto_Sha256Abort(&hash);
            (void) port->source_close(port->context, handle);
            return IMAGE_INSTALL_READBACK;
        }
        offset += (uint32_t) received;
    }
    status = Crypto_Sha256Finish(&hash, digest);
    if (opened)
        (void) port->source_close(port->context, handle);
    if (FirmwareStatus_IsError(status))
        return IMAGE_INSTALL_IO;
    return memcmp(digest, plan->expected_sha256, sizeof(digest)) == 0 ? IMAGE_INSTALL_OK
                                                                      : IMAGE_INSTALL_HASH;
}

ImageInstallResult_t ImageInstaller_Install(const ImageInstallPlan_t *plan)
{
    return ImageInstaller_InstallWithPort(plan, &s_port);
}

const char *ImageInstaller_ResultString(ImageInstallResult_t result)
{
    static const char *const strings[] = {"ok",    "invalid argument", "source", "size", "erase",
                                          "write", "readback",         "hash",   "io"};
    return (result >= 0 && (size_t) result < sizeof(strings) / sizeof(strings[0])) ? strings[result]
                                                                                   : "unknown";
}
