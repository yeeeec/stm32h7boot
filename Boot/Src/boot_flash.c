#include "boot_flash.h"

#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "boot_crc32.h"

#define BOOT_FLASHWORD_SIZE              32U
#define BOOT_FLASH_SECTOR_SIZE           (128UL * 1024UL)
#define BOOT_FLASH_LAST_SECTOR_PER_BANK  (FLASH_SECTOR_TOTAL - 1U)

static const BootPartition g_boot_partitions[] =
{
  { 0U, 0U },
  { BOOT_APP1_BASE, BOOT_APP1_SIZE },
  { BOOT_APP2_BASE, BOOT_APP2_SIZE }
};

static uint32_t Boot_Flash_CalcCtrlCrc(const BootControlBlock *ctrl)
{
  return Boot_Crc32_Calc(ctrl, offsetof(BootControlBlock, crc32), 0xFFFFFFFFUL);
}

static int Boot_Flash_IsCtrlValid(const BootControlBlock *ctrl)
{
  if (ctrl == NULL)
  {
    return 0;
  }

  if ((ctrl->magic != BOOT_CTRL_MAGIC) ||
      (ctrl->struct_version != BOOT_CTRL_STRUCT_VERSION) ||
      (ctrl->length != sizeof(BootControlBlock)))
  {
    return 0;
  }

  return (Boot_Flash_CalcCtrlCrc(ctrl) == ctrl->crc32) ? 1 : 0;
}

static void Boot_Flash_ClearAllFlags(void)
{
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_EOP_BANK1);
#if defined(DUAL_BANK)
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK2 | FLASH_FLAG_EOP_BANK2);
#endif
}

static void Boot_Flash_RefreshCache(void)
{
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanInvalidateDCache();
  }

  if ((SCB->CCR & SCB_CCR_IC_Msk) != 0U)
  {
    SCB_InvalidateICache();
  }
}

static bool Boot_Flash_AddressToSector(uint32_t address, uint32_t *bank, uint32_t *sector)
{
  uint32_t sector_index;

  if ((bank == NULL) || (sector == NULL))
  {
    return false;
  }

  if ((address >= FLASH_BANK1_BASE) && (address <= FLASH_END))
  {
    if (address < FLASH_BANK2_BASE)
    {
      sector_index = (address - FLASH_BANK1_BASE) / BOOT_FLASH_SECTOR_SIZE;
      *bank = FLASH_BANK_1;
      *sector = sector_index;
      return (sector_index <= BOOT_FLASH_LAST_SECTOR_PER_BANK);
    }

#if defined(DUAL_BANK)
    sector_index = (address - FLASH_BANK2_BASE) / BOOT_FLASH_SECTOR_SIZE;
    *bank = FLASH_BANK_2;
    *sector = sector_index;
    return (sector_index <= BOOT_FLASH_LAST_SECTOR_PER_BANK);
#endif
  }

  return false;
}

static BootError Boot_Flash_EraseSectors(uint32_t bank, uint32_t start_sector, uint32_t count)
{
  FLASH_EraseInitTypeDef erase_init;
  uint32_t sector_error = 0xFFFFFFFFUL;
  HAL_StatusTypeDef hal_status;

  if (count == 0U)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memset(&erase_init, 0, sizeof(erase_init));
  erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase_init.Banks = bank;
  erase_init.Sector = start_sector;
  erase_init.NbSectors = count;
  erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  hal_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
  if (hal_status != HAL_OK)
  {
    (void)sector_error;
    return BOOT_ERR_FLASH_ERASE;
  }

  return BOOT_ERR_NONE;
}

void Boot_Flash_Init(void)
{
}

const BootPartition *Boot_Flash_GetPartition(BootSlot slot)
{
  if ((slot != BOOT_SLOT_APP1) && (slot != BOOT_SLOT_APP2))
  {
    return NULL;
  }

  return &g_boot_partitions[slot];
}

bool Boot_Flash_IsAddressInRange(uint32_t address, uint32_t size, uint32_t base, uint32_t range_size)
{
  uint32_t range_end;
  uint32_t address_end;

  if (size == 0U)
  {
    return false;
  }

  range_end = base + range_size;
  address_end = address + size;

  if ((address < base) || (address_end < address))
  {
    return false;
  }

  return (address_end <= range_end);
}

static bool Boot_Flash_IsWritableRange(uint32_t address, uint32_t size)
{
  if (Boot_Flash_IsAddressInRange(address, size, BOOT_APP1_BASE, BOOT_APP1_SIZE))
  {
    return true;
  }

  if (Boot_Flash_IsAddressInRange(address, size, BOOT_APP2_BASE, BOOT_APP2_SIZE))
  {
    return true;
  }

  if (Boot_Flash_IsAddressInRange(address, size, BOOT_CONFIG_BASE, BOOT_CONFIG_SIZE))
  {
    return true;
  }

  return false;
}

BootError Boot_Flash_LoadControlBlock(BootControlBlock *ctrl)
{
  BootControlBlock copy_a;
  BootControlBlock copy_b;
  int valid_a;
  int valid_b;

  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  memcpy(&copy_a, (const void *)BOOT_CTRL_A_BASE, sizeof(copy_a));
  memcpy(&copy_b, (const void *)BOOT_CTRL_B_BASE, sizeof(copy_b));
  valid_a = Boot_Flash_IsCtrlValid(&copy_a);
  valid_b = Boot_Flash_IsCtrlValid(&copy_b);

  if ((valid_a == 0) && (valid_b == 0))
  {
    return BOOT_ERR_CTRL_NOT_FOUND;
  }

  if ((valid_a != 0) && ((valid_b == 0) || (copy_a.sequence >= copy_b.sequence)))
  {
    memcpy(ctrl, &copy_a, sizeof(*ctrl));
  }
  else
  {
    memcpy(ctrl, &copy_b, sizeof(*ctrl));
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Flash_SaveControlBlock(const BootControlBlock *ctrl)
{
  BootControlBlock copy_a;
  BootControlBlock copy_b;
  int valid_a;
  int valid_b;
  uint8_t write_to_a;
  BootError error;

  if (ctrl == NULL)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (Boot_Flash_IsCtrlValid(ctrl) == 0)
  {
    return BOOT_ERR_CTRL_CRC;
  }

  memcpy(&copy_a, (const void *)BOOT_CTRL_A_BASE, sizeof(copy_a));
  memcpy(&copy_b, (const void *)BOOT_CTRL_B_BASE, sizeof(copy_b));
  valid_a = Boot_Flash_IsCtrlValid(&copy_a);
  valid_b = Boot_Flash_IsCtrlValid(&copy_b);

  if ((valid_a != 0) && (valid_b != 0))
  {
    write_to_a = (copy_a.sequence <= copy_b.sequence) ? 1U : 0U;
  }
  else if (valid_b != 0)
  {
    write_to_a = 1U;
  }
  else
  {
    write_to_a = 0U;
  }

  error = Boot_Flash_Erase(BOOT_CONFIG_BASE, BOOT_CONFIG_SIZE);
  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  if (write_to_a != 0U)
  {
    if (valid_b != 0)
    {
      error = Boot_Flash_Write(BOOT_CTRL_B_BASE, &copy_b, sizeof(copy_b));
      if (error != BOOT_ERR_NONE)
      {
        return error;
      }
    }

    error = Boot_Flash_Write(BOOT_CTRL_A_BASE, ctrl, sizeof(*ctrl));
  }
  else
  {
    if (valid_a != 0)
    {
      error = Boot_Flash_Write(BOOT_CTRL_A_BASE, &copy_a, sizeof(copy_a));
      if (error != BOOT_ERR_NONE)
      {
        return error;
      }
    }

    error = Boot_Flash_Write(BOOT_CTRL_B_BASE, ctrl, sizeof(*ctrl));
  }

  if (error != BOOT_ERR_NONE)
  {
    return error;
  }

  return BOOT_ERR_NONE;
}

BootError Boot_Flash_Read(uint32_t address, void *buffer, uint32_t size)
{
  if ((buffer == NULL) || (size == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (Boot_Flash_IsAddressInRange(address, size, BOOT_INTERNAL_FLASH_BASE, BOOT_INTERNAL_FLASH_SIZE) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  memcpy(buffer, (const void *)address, size);
  return BOOT_ERR_NONE;
}

BootError Boot_Flash_Write(uint32_t address, const void *data, uint32_t size)
{
  HAL_StatusTypeDef hal_status;
  const uint8_t *source;
  uint32_t write_address;
  uint32_t remaining;

  if ((data == NULL) || (size == 0U))
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (Boot_Flash_IsAddressInRange(address, size, BOOT_INTERNAL_FLASH_BASE, BOOT_INTERNAL_FLASH_SIZE) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  if (Boot_Flash_IsWritableRange(address, size) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  hal_status = HAL_FLASH_Unlock();
  if (hal_status != HAL_OK)
  {
    return BOOT_ERR_FLASH_WRITE;
  }

  Boot_Flash_ClearAllFlags();

  source = (const uint8_t *)data;
  write_address = address;
  remaining = size;
  while (remaining > 0U)
  {
    uint32_t aligned_address = write_address & ~(BOOT_FLASHWORD_SIZE - 1U);
    uint32_t word_offset = write_address - aligned_address;
    uint32_t copy_size = BOOT_FLASHWORD_SIZE - word_offset;
    uint8_t flash_word[BOOT_FLASHWORD_SIZE];

    if (copy_size > remaining)
    {
      copy_size = remaining;
    }

    memcpy(flash_word, (const void *)aligned_address, sizeof(flash_word));
    memcpy(&flash_word[word_offset], source, copy_size);

    hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                                   aligned_address,
                                   (uint32_t)(uintptr_t)flash_word);
    if (hal_status != HAL_OK)
    {
      (void)HAL_FLASH_Lock();
      return BOOT_ERR_FLASH_WRITE;
    }

    if (memcmp((const void *)aligned_address, flash_word, sizeof(flash_word)) != 0)
    {
      (void)HAL_FLASH_Lock();
      return BOOT_ERR_FLASH_VERIFY;
    }

    write_address += copy_size;
    source += copy_size;
    remaining -= copy_size;
  }

  Boot_Flash_RefreshCache();
  (void)HAL_FLASH_Lock();
  return BOOT_ERR_NONE;
}

BootError Boot_Flash_Erase(uint32_t address, uint32_t size)
{
  HAL_StatusTypeDef hal_status;
  uint32_t start_bank;
  uint32_t end_bank;
  uint32_t start_sector;
  uint32_t end_sector;
  uint32_t end_address;
  BootError error;

  if (size == 0U)
  {
    return BOOT_ERR_INVALID_ARGUMENT;
  }

  if (Boot_Flash_IsAddressInRange(address, size, BOOT_INTERNAL_FLASH_BASE, BOOT_INTERNAL_FLASH_SIZE) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  if (Boot_Flash_IsWritableRange(address, size) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  end_address = address + size - 1U;
  if (Boot_Flash_AddressToSector(address, &start_bank, &start_sector) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  if (Boot_Flash_AddressToSector(end_address, &end_bank, &end_sector) == false)
  {
    return BOOT_ERR_FLASH_RANGE;
  }

  hal_status = HAL_FLASH_Unlock();
  if (hal_status != HAL_OK)
  {
    return BOOT_ERR_FLASH_ERASE;
  }

  Boot_Flash_ClearAllFlags();

  if (start_bank == end_bank)
  {
    error = Boot_Flash_EraseSectors(start_bank, start_sector, (end_sector - start_sector) + 1U);
  }
#if defined(DUAL_BANK)
  else if ((start_bank == FLASH_BANK_1) && (end_bank == FLASH_BANK_2))
  {
    error = Boot_Flash_EraseSectors(FLASH_BANK_1,
                                    start_sector,
                                    (BOOT_FLASH_LAST_SECTOR_PER_BANK - start_sector) + 1U);
    if (error == BOOT_ERR_NONE)
    {
      error = Boot_Flash_EraseSectors(FLASH_BANK_2, 0U, end_sector + 1U);
    }
  }
#endif
  else
  {
    error = BOOT_ERR_FLASH_RANGE;
  }

  if (error == BOOT_ERR_NONE)
  {
    Boot_Flash_RefreshCache();
  }

  (void)HAL_FLASH_Lock();
  return error;
}
