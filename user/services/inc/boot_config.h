#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_PROJECT_VERSION                "0.1.0"

#define BOOT_INTERNAL_FLASH_BASE            0x08000000UL
#define BOOT_INTERNAL_FLASH_SIZE            (2048UL * 1024UL)
#define BOOT_INTERNAL_FLASH_END             (BOOT_INTERNAL_FLASH_BASE + BOOT_INTERNAL_FLASH_SIZE)

#define BOOT_BOOT_BASE                      0x08000000UL
#define BOOT_BOOT_SIZE                      (128UL * 1024UL)

#define BOOT_INFO_BASE                      0x08020000UL
#define BOOT_INFO_SIZE                      (128UL * 1024UL)

#define BOOT_EXTFLASH_BASE                  0x90000000UL
#define BOOT_EXTFLASH_ALLOWED_OFFSET        0x00000000UL
#define BOOT_EXTFLASH_ALLOWED_SIZE          (8UL * 1024UL * 1024UL)
#define BOOT_EXTFLASH_END                   (BOOT_EXTFLASH_BASE + BOOT_EXTFLASH_ALLOWED_SIZE)

#define BOOT_APP_SLOT_A_BASE                0x90000000UL
#define BOOT_APP_SLOT_B_BASE                0x90080000UL
#define BOOT_APP_SLOT_SIZE                  (512UL * 1024UL)
#define BOOT_DEFAULT_APP_BASE               BOOT_APP_SLOT_A_BASE
#define BOOT_DEFAULT_APP_SIZE               BOOT_APP_SLOT_SIZE
#define BOOT_APP_SLOT_COUNT                 2U
#define BOOT_IMAGE_EXPECTED_WRITE_ADDRESS   BOOT_APP_SLOT_A_BASE

#define BOOT_EXTFLASH_PAGE_SIZE             256U
#define BOOT_EXTFLASH_SECTOR_SIZE           (4UL * 1024UL)
#define BOOT_EXTFLASH_CMD_TIMEOUT_MS        100U
#define BOOT_EXTFLASH_WRITE_TIMEOUT_MS      1000U
#define BOOT_EXTFLASH_ERASE_TIMEOUT_MS      5000U

#define BOOT_DTCM_BASE                      0x20000000UL
#define BOOT_DTCM_SIZE                      (128UL * 1024UL)
#define BOOT_AXI_SRAM_BASE                  0x24000000UL
#define BOOT_AXI_SRAM_SIZE                  (512UL * 1024UL)
#define BOOT_KEEP_RAM_SIZE                  (8UL * 1024UL)
#define BOOT_KEEP_RAM_BASE                  (BOOT_AXI_SRAM_BASE + BOOT_AXI_SRAM_SIZE - BOOT_KEEP_RAM_SIZE)
#define BOOT_SRAM_D2_BASE                   0x30000000UL
#define BOOT_SRAM_D2_SIZE                   (288UL * 1024UL)
#define BOOT_SRAM_D3_BASE                   0x38000000UL
#define BOOT_SRAM_D3_SIZE                   (64UL * 1024UL)

#define BOOT_USB_WAIT_WINDOW_MS             600UL
#define BOOT_PENDING_SLOT_MAX_ATTEMPTS      3U

#define BOOT_FILE_NAME_LENGTH               32U
#define BOOT_FILE_PATH_LENGTH               48U
#define BOOT_BUILD_TIME_LENGTH              20U
#define BOOT_VERSION_INFO_SIZE              96U
#define BOOT_VERSION_MAGIC_LENGTH           4U
#define BOOT_VERSION_PROJECT_NAME_LENGTH    32U
#define BOOT_VERSION_IMAGE_TAG_LENGTH       4U
#define BOOT_VERSION_GIT_HASH_LENGTH        8U
#define BOOT_VERSION_RESERVED_SIZE          16U
#define BOOT_VERSION_MAGIC                  "FWVH"
#define BOOT_VERSION_IMAGE_TAG              "FWPK"
#define BOOT_VERSION_CRC32_OFFSET           0x44U

#define BOOT_USB_BOOT_DIR                   "/bin"
#define BOOT_USB_APP_IMAGE_NAME             "app.bin"
#define BOOT_USB_APP_IMAGE_PATH             "/bin/app.bin"
#define BOOT_UDISK_CHECK_ENABLE             1U
#define BOOT_UDISK_CHECK_PATH               "/cck"
#define BOOT_UDISK_CHECK_SALT               "MySecretSalt2024"
#define BOOT_UDISK_CCK_SIZE                 4U

#define BOOT_UPGRADE_READ_CHUNK_SIZE        1024U
#define BOOT_UPGRADE_MAX_RETRIES            3U
#define BOOT_INFO_RECORD_SIZE               64U
#define BOOT_INFO_JOURNAL_SIZE              BOOT_INFO_SIZE
#define BOOT_INFO_RECORD_COUNT              (BOOT_INFO_JOURNAL_SIZE / BOOT_INFO_RECORD_SIZE)

#define BOOT_LOG_MESSAGE_MAX_LENGTH         96U
#define BOOT_LOG_BUFFER_DEPTH               24U
#define BOOT_LOG_UART_TIMEOUT_MS            20U

#endif
