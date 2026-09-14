/**
 * @file sdram.h
 * @brief Hardware-independent SDR SDRAM initialization sequence.
 *
 * The driver owns SDRAM command ordering only. FMC handles, command targets,
 * clock calculation, mode-register values, and board timing belong to the BSP
 * implementation of sdram_port_t and sdram_config_t.
 */
#ifndef DEVICE_SDRAM_H
#define DEVICE_SDRAM_H

#include <stdint.h>

#include "firmware/status.h"

typedef enum
{
    /* Start the SDRAM clock before issuing device commands. */
    SDRAM_COMMAND_CLOCK_ENABLE = 0,
    /* Precharge every internal bank. */
    SDRAM_COMMAND_PRECHARGE_ALL,
    /* Run the configured number of auto-refresh cycles. */
    SDRAM_COMMAND_AUTO_REFRESH,
    /* Program burst, CAS, and write-burst behavior. */
    SDRAM_COMMAND_LOAD_MODE
} sdram_command_t;

/*
 * Port callbacks are synchronous. They must not retain transaction arguments
 * after returning and must translate controller errors to firmware_status_t.
 */
typedef firmware_status_t (*sdram_send_command_fn)(void *context, sdram_command_t command,
                                                   uint32_t auto_refresh_count,
                                                   uint32_t mode_register);
typedef firmware_status_t (*sdram_set_refresh_rate_fn)(void *context, uint32_t refresh_rate);
typedef void (*sdram_delay_ms_fn)(void *context, uint32_t delay_ms);

typedef struct
{
    /* Passed unchanged to every callback; ownership remains with the caller. */
    void *context;
    sdram_send_command_fn send_command;
    sdram_set_refresh_rate_fn set_refresh_rate;
    sdram_delay_ms_fn delay_ms;
} sdram_port_t;

typedef struct
{
    /* Delay after clock enable; must satisfy the SDRAM power-up requirement. */
    uint32_t startup_delay_ms;
    /* Number of refresh cycles issued during initialization, commonly eight. */
    uint32_t auto_refresh_count;
    /* Device-specific SDRAM mode-register bit pattern. */
    uint32_t mode_register;
    /* Controller refresh counter derived from SDCLK and the row count. */
    uint32_t refresh_rate;
} sdram_config_t;

/* Driver objects must be zero-initialized and have static caller-owned storage. */
typedef struct sdram
{
    sdram_port_t port;
    int initialized;
} sdram_t;

firmware_status_t Sdram_Init(sdram_t *device, const sdram_port_t *port,
                             const sdram_config_t *config);

#endif
