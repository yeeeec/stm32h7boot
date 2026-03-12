#include "boot/boot_ports.h"

#include "main.h"

void boot_port_get_capability(boot_port_capability_t *capability)
{
    if (capability == 0)
    {
        return;
    }

    capability->usb_loader = 0U;
    capability->image_validator = 0U;
    capability->ext_flash_programmer = 0U;
    capability->xip_mapper = 0U;
    capability->app_jumper = 0U;
}

uint32_t boot_port_get_time_ms(void)
{
    return HAL_GetTick();
}

boot_result_t boot_port_usb_is_ready(void)
{
    return BOOT_RESULT_NOT_READY;
}

boot_result_t boot_port_usb_load_package(const char *path,
                                         uint8_t *buffer,
                                         uint32_t capacity,
                                         boot_image_metadata_t *metadata)
{
    (void)path;
    (void)buffer;
    (void)capacity;
    (void)metadata;
    return BOOT_RESULT_NOT_IMPLEMENTED;
}

boot_result_t boot_port_verify_package(const uint8_t *buffer,
                                       uint32_t size,
                                       const boot_image_metadata_t *metadata)
{
    (void)buffer;
    (void)size;
    (void)metadata;
    return BOOT_RESULT_NOT_IMPLEMENTED;
}

boot_result_t boot_port_program_external_flash(uint32_t ext_flash_addr,
                                               const uint8_t *buffer,
                                               uint32_t size)
{
    (void)ext_flash_addr;
    (void)buffer;
    (void)size;
    return BOOT_RESULT_NOT_IMPLEMENTED;
}

boot_result_t boot_port_enter_xip_mode(void)
{
    return BOOT_RESULT_NOT_IMPLEMENTED;
}

boot_result_t boot_port_jump_to_app(uint32_t vector_addr)
{
    (void)vector_addr;
    return BOOT_RESULT_NOT_IMPLEMENTED;
}
