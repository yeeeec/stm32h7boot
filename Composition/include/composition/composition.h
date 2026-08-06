/**
 * @file composition.h
 * @brief Composition-root initialization for firmware dependencies.
 */
#ifndef COMPOSITION_H
#define COMPOSITION_H

#include <stdint.h>

#include "firmware/status.h"

struct manifest_service;

#define COMPOSITION_MANIFEST_PUBLIC_KEY_SIZE 64U
#define COMPOSITION_MANIFEST_KEY_ID_MAX_SIZE 31U

/**
 * @brief Configure the production Manifest verification key.
 *
 * The key and Key ID are copied into Composition-owned storage. This function
 * must be called at most once, before Composition_Init. No default or test key
 * is compiled into the firmware.
 */
firmware_status_t Composition_ConfigureManifestVerifier(
    const uint8_t public_key[COMPOSITION_MANIFEST_PUBLIC_KEY_SIZE],
    const char *key_id);

/**
 * @brief Bind concrete platform and BSP implementations to application services.
 *
 * This function configures the logger, validates the external-flash geometry,
 * constructs available storage, checksum, package, validation, XIP, and jump
 * objects. It is intentionally the single ownership point for static instances.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_STATE if called more than once or a required
 *         dependency is not initialized.
 * @return A dependency-specific failure status otherwise.
 *
 * @pre Platform_Init and BSP_Init have completed successfully.
 */
firmware_status_t Composition_Init(void);

/** Return the initialized Manifest service, or NULL when no key is configured. */
struct manifest_service *Composition_ManifestService(void);

/** Return nonzero when a production Manifest key has been configured. */
int Composition_IsManifestVerifierConfigured(void);

/**
 * @brief Query whether the dependency graph has been initialized.
 *
 * @return Nonzero after successful Composition_Init; zero otherwise.
 */
int Composition_IsInitialized(void);

#endif
