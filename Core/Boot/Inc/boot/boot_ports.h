#ifndef BOOT_PORTS_H
#define BOOT_PORTS_H

#include <stdint.h>

#include "boot/boot_types.h"

typedef struct
{
    uint32_t image_size;
    uint32_t image_crc32;
} boot_image_metadata_t;

typedef struct
{
    uint8_t usb_loader;
    uint8_t image_validator;
    uint8_t ext_flash_programmer;
    uint8_t xip_mapper;
    uint8_t app_jumper;
} boot_port_capability_t;

void boot_port_get_capability(boot_port_capability_t *capability);
uint32_t boot_port_get_time_ms(void);

boot_result_t boot_port_usb_is_ready(void);
boot_result_t boot_port_usb_load_package(const char *path,
                                         uint8_t *buffer,
                                         uint32_t capacity,
                                         boot_image_metadata_t *metadata);
boot_result_t boot_port_verify_package(const uint8_t *buffer,
                                       uint32_t size,
                                       const boot_image_metadata_t *metadata);
boot_result_t boot_port_program_external_flash(uint32_t ext_flash_addr,
                                               const uint8_t *buffer,
                                               uint32_t size);
boot_result_t boot_port_enter_xip_mode(void);
boot_result_t boot_port_jump_to_app(uint32_t vector_addr);

#endif /* BOOT_PORTS_H */
