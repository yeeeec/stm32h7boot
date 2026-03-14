#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdint.h>

#include "boot_error.h"
#include "boot_types.h"

BootError Boot_Image_ReadHeader(BootSlot slot, BootImageHeader *header);
BootError Boot_Image_GetVectorTableAddress(BootSlot slot, uint32_t *vector_table_address);
BootError Boot_Image_ValidateSlot(BootSlot slot, BootImageRecord *record);

#endif
