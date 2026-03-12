#include "boot/boot_ports.h"

#include <stdio.h>
#include <string.h>

#include "boot/boot_config.h"
#include "fatfs.h"
#include "main.h"
#include "usb_host.h"

extern ApplicationTypeDef Appli_state;
extern USBH_HandleTypeDef hUsbHostHS;

#define BOOT_PORT_FLASH_CMD_WRITE_ENABLE     (0x06U)
#define BOOT_PORT_FLASH_CMD_READ_STATUS_REG1 (0x05U)
#define BOOT_PORT_FLASH_CMD_SECTOR_ERASE_4K  (0x20U)
#define BOOT_PORT_FLASH_CMD_PAGE_PROGRAM     (0x02U)
#define BOOT_PORT_FLASH_CMD_FAST_READ        (0x0BU)

#define BOOT_PORT_FLASH_STATUS_BUSY_MASK     (0x01U)
#define BOOT_PORT_FLASH_PAGE_SIZE            (256U)
#define BOOT_PORT_FLASH_SECTOR_SIZE          (4096U)
#define BOOT_PORT_FLASH_ADDR_LIMIT           (0x01000000UL)

#define BOOT_PORT_QSPI_TIMEOUT_MS            (2000U)
#define BOOT_PORT_QSPI_ERASE_TIMEOUT_MS      (5000U)
#define BOOT_PORT_QSPI_PROGRAM_TIMEOUT_MS    (500U)
#define BOOT_PORT_QSPI_POLL_TIMEOUT_MS       (10000U)

#define BOOT_PORT_JUMP_IRQ_REG_COUNT         (8U)
#define BOOT_PORT_USB_MOUNT_RETRY_COUNT      (5U)
#define BOOT_PORT_USB_MOUNT_RETRY_DELAY_MS   (100U)

static QSPI_HandleTypeDef s_hqspi;
static uint8_t s_qspi_inited;
static uint8_t s_qspi_memory_mapped;

static uint32_t boot_port_crc32_calculate(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i = 0U;

    while (i < size)
    {
        uint32_t bit = 0U;
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
        i++;
    }

    return ~crc;
}

static uint8_t boot_port_is_valid_stack_pointer(uint32_t address)
{
    if ((address >= 0x20000000UL) && (address < 0x20020000UL))
    {
        return 1U;
    }
    if ((address >= 0x24000000UL) && (address < 0x24080000UL))
    {
        return 1U;
    }
    if ((address >= 0x30000000UL) && (address < 0x30048000UL))
    {
        return 1U;
    }
    if ((address >= 0x38000000UL) && (address < 0x38010000UL))
    {
        return 1U;
    }

    return 0U;
}

static boot_result_t boot_port_build_usb_path(const char *input, char *output, uint32_t output_size)
{
    int written = 0;

    if ((input == 0) || (output == 0) || (output_size == 0U))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    if (strchr(input, ':') != 0)
    {
        written = snprintf(output, (size_t)output_size, "%s", input);
    }
    else
    {
        written = snprintf(output,
                           (size_t)output_size,
                           "%s%s%s",
                           USBHPath,
                           (input[0] == '/') ? "" : "/",
                           input);
    }

    if ((written <= 0) || ((uint32_t)written >= output_size))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    return BOOT_RESULT_OK;
}

static boot_result_t boot_port_qspi_init(void)
{
    if (s_qspi_inited != 0U)
    {
        return BOOT_RESULT_OK;
    }

    (void)memset(&s_hqspi, 0, sizeof(s_hqspi));
    s_hqspi.Instance = QUADSPI;
    s_hqspi.Init.ClockPrescaler = 1U;
    s_hqspi.Init.FifoThreshold = 4U;
    s_hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
    s_hqspi.Init.FlashSize = 25U;
    s_hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_2_CYCLE;
    s_hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
    s_hqspi.Init.FlashID = QSPI_FLASH_ID_1;
    s_hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;

    if (HAL_QSPI_Init(&s_hqspi) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    s_qspi_inited = 1U;
    s_qspi_memory_mapped = 0U;
    return BOOT_RESULT_OK;
}

static boot_result_t boot_port_qspi_command_only(uint8_t instruction)
{
    QSPI_CommandTypeDef cmd;

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = instruction;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_NONE;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&s_hqspi, &cmd, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    return BOOT_RESULT_OK;
}

static boot_result_t boot_port_qspi_read_status_reg1(uint8_t *status)
{
    QSPI_CommandTypeDef cmd;

    if (status == 0)
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = BOOT_PORT_FLASH_CMD_READ_STATUS_REG1;
    cmd.AddressMode = QSPI_ADDRESS_NONE;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 1U;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&s_hqspi, &cmd, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }
    if (HAL_QSPI_Receive(&s_hqspi, status, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    return BOOT_RESULT_OK;
}

static boot_result_t boot_port_qspi_write_enable(void)
{
    return boot_port_qspi_command_only(BOOT_PORT_FLASH_CMD_WRITE_ENABLE);
}

static boot_result_t boot_port_qspi_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t status = 0U;
        boot_result_t result = boot_port_qspi_read_status_reg1(&status);
        if (result != BOOT_RESULT_OK)
        {
            return result;
        }
        if ((status & BOOT_PORT_FLASH_STATUS_BUSY_MASK) == 0U)
        {
            return BOOT_RESULT_OK;
        }
        HAL_Delay(1U);
    }

    return BOOT_RESULT_IO_ERROR;
}

static boot_result_t boot_port_qspi_erase_sector_4k(uint32_t address)
{
    QSPI_CommandTypeDef cmd;
    boot_result_t result = boot_port_qspi_write_enable();

    if (result != BOOT_RESULT_OK)
    {
        return result;
    }

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = BOOT_PORT_FLASH_CMD_SECTOR_ERASE_4K;
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize = QSPI_ADDRESS_24_BITS;
    cmd.Address = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_NONE;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&s_hqspi, &cmd, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    return boot_port_qspi_wait_ready(BOOT_PORT_QSPI_ERASE_TIMEOUT_MS);
}

static boot_result_t boot_port_qspi_program_page(uint32_t address, const uint8_t *data, uint32_t size)
{
    QSPI_CommandTypeDef cmd;
    boot_result_t result = BOOT_RESULT_OK;

    if ((data == 0) || (size == 0U) || (size > BOOT_PORT_FLASH_PAGE_SIZE))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    result = boot_port_qspi_write_enable();
    if (result != BOOT_RESULT_OK)
    {
        return result;
    }

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = BOOT_PORT_FLASH_CMD_PAGE_PROGRAM;
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize = QSPI_ADDRESS_24_BITS;
    cmd.Address = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = size;
    cmd.DummyCycles = 0U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&s_hqspi, &cmd, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }
    if (HAL_QSPI_Transmit(&s_hqspi, (uint8_t *)data, BOOT_PORT_QSPI_PROGRAM_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    return boot_port_qspi_wait_ready(BOOT_PORT_QSPI_POLL_TIMEOUT_MS);
}

static boot_result_t boot_port_qspi_read(uint32_t address, uint8_t *buffer, uint32_t size)
{
    QSPI_CommandTypeDef cmd;

    if ((buffer == 0) || (size == 0U))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = BOOT_PORT_FLASH_CMD_FAST_READ;
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize = QSPI_ADDRESS_24_BITS;
    cmd.Address = address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = size;
    cmd.DummyCycles = 8U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    if (HAL_QSPI_Command(&s_hqspi, &cmd, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }
    if (HAL_QSPI_Receive(&s_hqspi, buffer, BOOT_PORT_QSPI_TIMEOUT_MS) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    return BOOT_RESULT_OK;
}

void boot_port_get_capability(boot_port_capability_t *capability)
{
    if (capability == 0)
    {
        return;
    }

    capability->usb_loader = 1U;
    capability->image_validator = 1U;
    capability->ext_flash_programmer = 1U;
    capability->xip_mapper = 1U;
    capability->app_jumper = 1U;
}

uint32_t boot_port_get_time_ms(void)
{
    return HAL_GetTick();
}

boot_result_t boot_port_usb_is_ready(void)
{
    if (Appli_state == APPLICATION_READY)
    {
        return BOOT_RESULT_OK;
    }

    return BOOT_RESULT_NOT_READY;
}

boot_result_t boot_port_usb_load_package(const char *path,
                                         uint8_t *buffer,
                                         uint32_t capacity,
                                         boot_image_metadata_t *metadata)
{
    FIL file;
    char full_path[64];
    UINT bytes_read = 0U;
    FRESULT fr = FR_OK;
    FSIZE_t file_size = 0U;
    boot_result_t result = BOOT_RESULT_OK;
    uint32_t retry = 0U;

    if ((path == 0) || (buffer == 0) || (metadata == 0) || (capacity == 0U))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    if (boot_port_usb_is_ready() != BOOT_RESULT_OK)
    {
        return BOOT_RESULT_NOT_READY;
    }
    if (USBH_MSC_UnitIsReady(&hUSB_Host, 0U) == 0U)
    {
        return BOOT_RESULT_NOT_READY;
    }

    result = boot_port_build_usb_path(path, full_path, (uint32_t)sizeof(full_path));
    if (result != BOOT_RESULT_OK)
    {
        return result;
    }

    for (retry = 0U; retry < BOOT_PORT_USB_MOUNT_RETRY_COUNT; retry++)
    {
        fr = f_mount(&USBHFatFS, USBHPath, 1U);
        if (fr == FR_OK)
        {
            break;
        }
        if ((fr != FR_NOT_READY) && (fr != FR_DISK_ERR) && (fr != FR_NO_FILESYSTEM))
        {
            return BOOT_RESULT_IO_ERROR;
        }
        HAL_Delay(BOOT_PORT_USB_MOUNT_RETRY_DELAY_MS);
    }
    if (fr != FR_OK)
    {
        return (fr == FR_NO_FILESYSTEM) ? BOOT_RESULT_VERIFY_ERROR : BOOT_RESULT_NOT_READY;
    }

    fr = f_open(&file, full_path, FA_READ);
    if (fr != FR_OK)
    {
        if ((fr == FR_NO_FILE) || (fr == FR_NO_PATH) || (fr == FR_NOT_READY))
        {
            return BOOT_RESULT_NOT_READY;
        }
        return BOOT_RESULT_IO_ERROR;
    }

    file_size = f_size(&file);
    if ((file_size == 0U) || (file_size > (FSIZE_t)capacity))
    {
        (void)f_close(&file);
        return BOOT_RESULT_INVALID_PARAM;
    }

    fr = f_read(&file, buffer, (UINT)file_size, &bytes_read);
    (void)f_close(&file);
    if ((fr != FR_OK) || (bytes_read != (UINT)file_size))
    {
        return BOOT_RESULT_IO_ERROR;
    }

    metadata->image_size = (uint32_t)file_size;
    metadata->image_crc32 = boot_port_crc32_calculate(buffer, metadata->image_size);

    return BOOT_RESULT_OK;
}

boot_result_t boot_port_verify_package(const uint8_t *buffer,
                                       uint32_t size,
                                       const boot_image_metadata_t *metadata)
{
    uint32_t crc = 0U;

    if ((buffer == 0) || (metadata == 0) || (size == 0U))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    if (size != metadata->image_size)
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }

    crc = boot_port_crc32_calculate(buffer, size);
    if (crc != metadata->image_crc32)
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }

    if (size < 8U)
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }

    return BOOT_RESULT_OK;
}

boot_result_t boot_port_program_external_flash(uint32_t ext_flash_addr,
                                               const uint8_t *buffer,
                                               uint32_t size)
{
    uint32_t start = 0U;
    uint32_t end = 0U;
    uint32_t addr = 0U;
    uint32_t offset = 0U;
    boot_result_t result = BOOT_RESULT_OK;

    if ((buffer == 0) || (size == 0U))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }
    if ((ext_flash_addr >= BOOT_PORT_FLASH_ADDR_LIMIT) ||
        (size > BOOT_PORT_FLASH_ADDR_LIMIT) ||
        (ext_flash_addr > (BOOT_PORT_FLASH_ADDR_LIMIT - size)))
    {
        return BOOT_RESULT_INVALID_PARAM;
    }

    result = boot_port_qspi_init();
    if (result != BOOT_RESULT_OK)
    {
        return result;
    }

    if (s_qspi_memory_mapped != 0U)
    {
        if (HAL_QSPI_Abort(&s_hqspi) != HAL_OK)
        {
            return BOOT_RESULT_IO_ERROR;
        }
        s_qspi_memory_mapped = 0U;
    }

    start = ext_flash_addr & ~(BOOT_PORT_FLASH_SECTOR_SIZE - 1U);
    end = (ext_flash_addr + size - 1U) & ~(BOOT_PORT_FLASH_SECTOR_SIZE - 1U);
    addr = start;
    while (addr <= end)
    {
        result = boot_port_qspi_erase_sector_4k(addr);
        if (result != BOOT_RESULT_OK)
        {
            return result;
        }
        addr += BOOT_PORT_FLASH_SECTOR_SIZE;
    }

    offset = 0U;
    while (offset < size)
    {
        uint32_t page_offset = (ext_flash_addr + offset) % BOOT_PORT_FLASH_PAGE_SIZE;
        uint32_t chunk = BOOT_PORT_FLASH_PAGE_SIZE - page_offset;
        if (chunk > (size - offset))
        {
            chunk = size - offset;
        }

        result = boot_port_qspi_program_page(ext_flash_addr + offset, &buffer[offset], chunk);
        if (result != BOOT_RESULT_OK)
        {
            return result;
        }

        offset += chunk;
    }

    offset = 0U;
    while (offset < size)
    {
        uint8_t verify_buf[BOOT_PORT_FLASH_PAGE_SIZE];
        uint32_t chunk = BOOT_PORT_FLASH_PAGE_SIZE;

        if (chunk > (size - offset))
        {
            chunk = size - offset;
        }

        result = boot_port_qspi_read(ext_flash_addr + offset, verify_buf, chunk);
        if (result != BOOT_RESULT_OK)
        {
            return result;
        }
        if (memcmp(verify_buf, &buffer[offset], chunk) != 0)
        {
            return BOOT_RESULT_VERIFY_ERROR;
        }

        offset += chunk;
    }

    return BOOT_RESULT_OK;
}

boot_result_t boot_port_enter_xip_mode(void)
{
    QSPI_CommandTypeDef cmd;
    QSPI_MemoryMappedTypeDef mem_cfg;
    boot_result_t result = boot_port_qspi_init();

    if (result != BOOT_RESULT_OK)
    {
        return result;
    }

    if (s_qspi_memory_mapped != 0U)
    {
        return BOOT_RESULT_OK;
    }

    (void)memset(&cmd, 0, sizeof(cmd));
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.Instruction = BOOT_PORT_FLASH_CMD_FAST_READ;
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.AddressSize = QSPI_ADDRESS_24_BITS;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.DummyCycles = 8U;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;

    (void)memset(&mem_cfg, 0, sizeof(mem_cfg));
    mem_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
    mem_cfg.TimeOutPeriod = 0U;

    if (HAL_QSPI_MemoryMapped(&s_hqspi, &cmd, &mem_cfg) != HAL_OK)
    {
        return BOOT_RESULT_IO_ERROR;
    }

    SCB_CleanDCache();
    SCB_InvalidateDCache();
    SCB_InvalidateICache();
    __DSB();
    __ISB();

    s_qspi_memory_mapped = 1U;
    return BOOT_RESULT_OK;
}

boot_result_t boot_port_jump_to_app(uint32_t vector_addr)
{
    uint32_t app_msp = *(__IO uint32_t *)vector_addr;
    uint32_t app_reset_handler = *(__IO uint32_t *)(vector_addr + 4U);
    uint32_t i = 0U;
    void (*app_entry)(void) = 0;

    if (boot_port_is_valid_stack_pointer(app_msp) == 0U)
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }
    if ((app_reset_handler & 0x1U) == 0U)
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }
    if ((app_reset_handler < BOOT_CFG_XIP_BASE_ADDR) ||
        (app_reset_handler >= (BOOT_CFG_XIP_BASE_ADDR + BOOT_PORT_FLASH_ADDR_LIMIT)))
    {
        return BOOT_RESULT_VERIFY_ERROR;
    }

    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (i = 0U; i < BOOT_PORT_JUMP_IRQ_REG_COUNT; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = vector_addr;
    __DSB();
    __ISB();
    __set_MSP(app_msp);

    app_entry = (void (*)(void))app_reset_handler;
    app_entry();

    return BOOT_RESULT_FAIL;
}
