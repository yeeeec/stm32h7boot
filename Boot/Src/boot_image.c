#include "boot_image.h"

#include <string.h>

#include "boot_crc32.h"
#include "boot_flash.h"

static int Boot_Image_StringMatches(const char *field, uint32_t field_length, const char *expected)
{
  size_t expected_length;

  if ((field == NULL) || (expected == NULL))
  {
    return 0;
  }

  expected_length = strlen(expected);
  if (expected_length >= field_length)
  {
    return 0;
  }

  if (memcmp(field, expected, expected_length) != 0)
  {
    return 0;
  }

  for (size_t i = expected_length; i < field_length; ++i)
  {
    if (field[i] != '\0')
    {
      return 0;
    }
  }

  return 1;
}

static int Boot_Image_IsInPartition(uint32_t address, uint32_t size, const BootPartition *partition)
{
  if (partition == NULL)
  {
    return 0;
  }

  return Boot_Flash_IsAddressInRange(address, size, partition->base_address, partition->size) ? 1 : 0;
}

static int Boot_Image_IsValidRamAddress(uint32_t address)
{
  return (Boot_Flash_IsAddressInRange(address, sizeof(uint32_t), BOOT_DTCM_BASE, BOOT_DTCM_SIZE) ||
          Boot_Flash_IsAddressInRange(address, sizeof(uint32_t), BOOT_AXI_SRAM_BASE, BOOT_AXI_SRAM_SIZE) ||
          Boot_Flash_IsAddressInRange(address, sizeof(uint32_t), BOOT_SRAM_D2_BASE, BOOT_SRAM_D2_SIZE) ||
          Boot_Flash_IsAddressInRange(address, sizeof(uint32_t), BOOT_SRAM_D3_BASE, BOOT_SRAM_D3_SIZE)) ? 1 : 0;
}

BootError Boot_Image_ReadHeader(BootSlot slot, BootImageHeader *header)
{
  const BootPartition *partition;
  const BootImageHeader *image_header;

  if (header == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  partition = Boot_Flash_GetPartition(slot);
  if (partition == NULL)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  image_header = (const BootImageHeader *)partition->base_address;
  memcpy(header, image_header, sizeof(*header));

  if (header->magic != BOOT_IMAGE_MAGIC)
  {
    return BOOT_ERR_IMAGE_HEADER;
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Image_GetVectorTableAddress(BootSlot slot, uint32_t *vector_table_address)
{
  BootImageHeader header;
  BootError error;
  const BootPartition *partition;

  if (vector_table_address == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  partition = Boot_Flash_GetPartition(slot);
  if (partition == NULL)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  error = Boot_Image_ReadHeader(slot, &header);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  if ((header.header_size < sizeof(BootImageHeader)) ||
      (header.header_size >= partition->size))
  {
    return BOOT_ERR_IMAGE_HEADER;
  }

  *vector_table_address = partition->base_address + header.header_size;
  return BOOT_ERR_NONE;
}

BootError Boot_Image_ValidateSlot(BootSlot slot, BootImageRecord *record)
{
  BootImageHeader header;
  BootError error;
  const BootPartition *partition;
  uint32_t vector_table_address;
  uint32_t initial_stack_pointer;
  uint32_t reset_handler;
  uint32_t calculated_crc;

  if (record != NULL)
  {
    memset(record, 0, sizeof(*record));
  }

  partition = Boot_Flash_GetPartition(slot);
  if (partition == NULL)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  error = Boot_Image_ReadHeader(slot, &header);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  if (header.header_version != BOOT_IMAGE_HEADER_VERSION)
  {
    return BOOT_ERR_IMAGE_HEADER;
  }

  if (header.image_type != BOOT_IMAGE_TYPE_APP)
  {
    return BOOT_ERR_IMAGE_TYPE;
  }

  if (header.target_slot != (uint32_t)slot)
  {
    return BOOT_ERR_IMAGE_SLOT;
  }

  if ((header.header_size < sizeof(BootImageHeader)) ||
      ((header.header_size + header.image_size) > partition->size))
  {
    return BOOT_ERR_IMAGE_SIZE;
  }

  vector_table_address = partition->base_address + header.header_size;
  if (Boot_Image_IsInPartition(vector_table_address, sizeof(uint32_t) * 2U, partition) == 0)
  {
    return BOOT_ERR_IMAGE_VECTOR;
  }

  if (Boot_Image_IsInPartition(header.entry_address, sizeof(uint32_t), partition) == 0)
  {
    return BOOT_ERR_IMAGE_VECTOR;
  }

  initial_stack_pointer = *(const uint32_t *)vector_table_address;
  reset_handler = *(const uint32_t *)(vector_table_address + sizeof(uint32_t));

  if (Boot_Image_IsValidRamAddress(initial_stack_pointer) == 0)
  {
    return BOOT_ERR_IMAGE_VECTOR;
  }

  if (Boot_Image_IsInPartition(reset_handler, sizeof(uint32_t), partition) == 0)
  {
    return BOOT_ERR_IMAGE_VECTOR;
  }

  if (Boot_Image_StringMatches(header.product, sizeof(header.product), BOOT_PRODUCT_NAME) == 0)
  {
    return BOOT_ERR_PRODUCT_MISMATCH;
  }

  if (Boot_Image_StringMatches(header.board, sizeof(header.board), BOOT_BOARD_NAME) == 0)
  {
    return BOOT_ERR_BOARD_MISMATCH;
  }

  calculated_crc = Boot_Crc32_Calc((const void *)vector_table_address, header.image_size, 0xFFFFFFFFUL);
  if (calculated_crc != header.image_crc32)
  {
    return BOOT_ERR_IMAGE_CRC;
  }

  if (record != NULL)
  {
    record->version = header.version;
    record->image_size = header.image_size;
    record->image_crc32 = header.image_crc32;
    record->flags = header.flags;
    record->is_valid = 1U;
  }

  return BOOT_ERR_NONE;
}
