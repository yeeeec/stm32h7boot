#include "boot_extflash.h"

#include "boot_config.h"

bool Boot_ExtFlash_IsRangeAllowed(uint32_t offset, uint32_t size)
{
  uint32_t relative_offset;

  if (size == 0U)
  {
    return false;
  }

#if (BOOT_EXTFLASH_ALLOWED_OFFSET > 0U)
  if (offset < BOOT_EXTFLASH_ALLOWED_OFFSET)
  {
    return false;
  }
#endif

  if (size > BOOT_EXTFLASH_ALLOWED_SIZE)
  {
    return false;
  }

  relative_offset = offset - BOOT_EXTFLASH_ALLOWED_OFFSET;
  return (relative_offset <= (BOOT_EXTFLASH_ALLOWED_SIZE - size));
}

BootError Boot_ExtFlash_Erase(uint32_t offset, uint32_t size)
{
  if (Boot_ExtFlash_IsRangeAllowed(offset, size) == false)
  {
    return BOOT_ERR_EXTFLASH_RANGE;
  }

  return BOOT_ERR_NOT_IMPLEMENTED;
}

BootError Boot_ExtFlash_Write(uint32_t offset, const void *data, uint32_t size)
{
  (void)data;

  if (Boot_ExtFlash_IsRangeAllowed(offset, size) == false)
  {
    return BOOT_ERR_EXTFLASH_RANGE;
  }

  return BOOT_ERR_NOT_IMPLEMENTED;
}

BootError Boot_ExtFlash_DumpToUsb(uint32_t offset, uint32_t size, const char *relative_path)
{
  (void)relative_path;

  if (Boot_ExtFlash_IsRangeAllowed(offset, size) == false)
  {
    return BOOT_ERR_EXTFLASH_RANGE;
  }

  return BOOT_ERR_NOT_IMPLEMENTED;
}
