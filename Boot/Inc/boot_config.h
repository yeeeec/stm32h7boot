#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

#define BOOT_PROJECT_VERSION                "0.1.0"

#define BOOT_VENDOR_NAME                    "DEFAULT"
#define BOOT_PRODUCT_NAME                   "STM32H7BOOT"
#define BOOT_BOARD_NAME                     "STM32H743"

#define BOOT_CTRL_MAGIC                     0x4243544CUL
#define BOOT_CTRL_STRUCT_VERSION            0x0001U

#define BOOT_IMAGE_MAGIC                    0x42494D47UL
#define BOOT_IMAGE_HEADER_VERSION           0x0001U

#define BOOT_INTERNAL_FLASH_BASE            0x08000000UL
#define BOOT_INTERNAL_FLASH_SIZE            (2048UL * 1024UL)

#define BOOT_BOOT_BASE                      0x08000000UL
#define BOOT_BOOT_SIZE                      (128UL * 1024UL)

#define BOOT_APP1_BASE                      0x08020000UL
#define BOOT_APP1_SIZE                      (896UL * 1024UL)
#define BOOT_APP_BASE                       BOOT_APP1_BASE
#define BOOT_APP_SIZE                       BOOT_APP1_SIZE

#define BOOT_CONFIG_BASE                    0x08100000UL
#define BOOT_CONFIG_SIZE                    (128UL * 1024UL)

#define BOOT_APP2_BASE                      0x08120000UL
#define BOOT_APP2_SIZE                      (896UL * 1024UL)

#define BOOT_CTRL_A_BASE                    0x08100000UL
#define BOOT_CTRL_A_SIZE                    (8UL * 1024UL)
#define BOOT_CTRL_B_BASE                    0x08102000UL
#define BOOT_CTRL_B_SIZE                    (8UL * 1024UL)
#define BOOT_SYSCONFIG_BASE                 0x08104000UL
#define BOOT_SYSCONFIG_SIZE                 (16UL * 1024UL)
#define BOOT_CONFIG_BACKUP_BASE             0x08108000UL
#define BOOT_CONFIG_BACKUP_SIZE             (16UL * 1024UL)
#define BOOT_SUMMARY_LOG_BASE               0x0810C000UL
#define BOOT_SUMMARY_LOG_SIZE               (32UL * 1024UL)

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
#define BOOT_RECOVERY_POLL_MS               1000UL
#define BOOT_PENDING_SLOT_MAX_ATTEMPTS      3U

#define BOOT_MANIFEST_MAX_OPERATIONS        8U
#define BOOT_MANIFEST_MAX_PRESERVE_ITEMS    8U
#define BOOT_MANIFEST_FILE_MAX_SIZE         4096U
#define BOOT_MANIFEST_TOKEN_MAX             256U
#define BOOT_FILE_NAME_LENGTH               32U
#define BOOT_FILE_PATH_LENGTH               48U
#define BOOT_NAME_LENGTH                    16U
#define BOOT_BUNDLE_NAME_LENGTH             24U
#define BOOT_MANIFEST_TEXT_LENGTH           32U
#define BOOT_BUILD_TIME_LENGTH              20U

#define BOOT_USB_BOOT_DIR                   "/bin"
#define BOOT_USB_OTHERS_INPUT_DIR           "/bin/crc"
#define BOOT_USB_CRC_DIR                    BOOT_USB_OTHERS_INPUT_DIR
#define BOOT_MANIFEST_PATH                  "/bin/manifest.json"
#define BOOT_DEVICE_CONFIG_PATH             "/bin/device.cfg"
#define BOOT_APP1_IMAGE_PATH                "/bin/app.bin"
#define BOOT_CONFIG_IMAGE_PATH              "/bin/config.bin"
#define BOOT_EXTFLASH_IMAGE_PATH            "/bin/resource.bin"
#define BOOT_SIGNATURE_PATH                 "/bin/signature.sig"
#define BOOT_LOG_DIR_PATH                   "/bin/log"
#define BOOT_LOG_FILE_PATH                  "/bin/log/boot.log"
#define BOOT_UDISK_CHECK_ENABLE             1U
#define BOOT_UDISK_CHECK_PATH               "/cck"
#define BOOT_UDISK_CHECK_SALT               "MySecretSalt2024"
#define BOOT_UDISK_CCK_SIZE                 4U

#define BOOT_UPGRADE_PACKET_FLAG            0xA5A55A5AUL
#define BOOT_UPGRADE_PACKET_HEADER_SIZE     8U
#define BOOT_UPGRADE_READ_CHUNK_SIZE        1024U
#define BOOT_UPGRADE_MAX_RETRIES            3U

#define BOOT_LOG_MESSAGE_MAX_LENGTH         96U
#define BOOT_LOG_BUFFER_DEPTH               24U
#define BOOT_LOG_UART_TIMEOUT_MS            20U

#define BOOT_EXTFLASH_ALLOWED_OFFSET        0x00000000UL
#define BOOT_EXTFLASH_ALLOWED_SIZE          (8UL * 1024UL * 1024UL)

#endif
