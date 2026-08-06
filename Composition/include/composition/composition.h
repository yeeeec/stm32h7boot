/**
 * @file composition.h
 * @brief Composition-root initialization for firmware dependencies.
 */
#ifndef COMPOSITION_H
#define COMPOSITION_H

#include "firmware/status.h"

/**
 * @brief Bind concrete platform and BSP implementations to application services.
 *
 * This function configures the logger, validates the external-flash geometry,
 * constructs storage, hash, package, update, recovery, validation, XIP, reset,
 * and jump objects, then configures Application. It is the single ownership
 * point for static instances.
 *
 * @return FIRMWARE_STATUS_OK on success.
 * @return FIRMWARE_STATUS_INVALID_STATE if called more than once or a required
 *         dependency is not initialized.
 * @return A dependency-specific failure status otherwise.
 *
 * @pre Platform_Init and BSP_Init have completed successfully.
 */
firmware_status_t Composition_Init(void);

/**
 * @brief Query whether the dependency graph has been initialized.
 *
 * @return Nonzero after successful Composition_Init; zero otherwise.
 */
int Composition_IsInitialized(void);

#endif
