/**
 * @file composition_config.h
 * @brief Product-level constants used while building the dependency graph.
 */
#ifndef COMPOSITION_CONFIG_H
#define COMPOSITION_CONFIG_H

#include "services/common/update_request_types.h"

/* STM32 Get-ID value confirmed for the secondary controller on this board. */
#ifndef COMPOSITION_SECONDARY_MCU_DEVICE_ID
#define COMPOSITION_SECONDARY_MCU_DEVICE_ID 0x0440U
#endif

#if (COMPOSITION_SECONDARY_MCU_DEVICE_ID > 0xFFFFU) || \
    (COMPOSITION_SECONDARY_MCU_DEVICE_ID == 0xFFFFU)
#error "COMPOSITION_SECONDARY_MCU_DEVICE_ID must be a confirmed 16-bit STM32 ID"
#endif

/* The target ROM implements AN3155 Extended Erase. */
#ifndef COMPOSITION_SECONDARY_MCU_USE_EXTENDED_ERASE
#define COMPOSITION_SECONDARY_MCU_USE_EXTENDED_ERASE 1
#endif

#if (COMPOSITION_SECONDARY_MCU_USE_EXTENDED_ERASE != 0) && \
    (COMPOSITION_SECONDARY_MCU_USE_EXTENDED_ERASE != 1)
#error "COMPOSITION_SECONDARY_MCU_USE_EXTENDED_ERASE must be 0 or 1"
#endif

/* Each ROM transaction is at most 256 bytes; keep both buffers cache aligned. */
#define COMPOSITION_SECONDARY_MCU_IO_BUFFER_SIZE 256U

/* Therapy MCU ROM target layout.  Confirm these values against the target MCU data sheet. */
#ifndef COMPOSITION_THERAPY_TARGET_ADDRESS
#define COMPOSITION_THERAPY_TARGET_ADDRESS 0x08000000UL
#endif
#ifndef COMPOSITION_THERAPY_TARGET_CAPACITY
#define COMPOSITION_THERAPY_TARGET_CAPACITY UPDATE_THERAPY_IMAGE_MAX_SIZE
#endif
#ifndef COMPOSITION_THERAPY_ERASE_PAGE_START
#define COMPOSITION_THERAPY_ERASE_PAGE_START 0U
#endif
#ifndef COMPOSITION_THERAPY_ERASE_PAGE_COUNT
#define COMPOSITION_THERAPY_ERASE_PAGE_COUNT 4U
#endif

#if (COMPOSITION_THERAPY_TARGET_CAPACITY != UPDATE_THERAPY_IMAGE_MAX_SIZE) || \
    (COMPOSITION_THERAPY_ERASE_PAGE_COUNT == 0U)
#error "Therapy target layout must expose a non-empty 512 KiB target"
#endif

#endif
