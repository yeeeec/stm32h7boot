#ifndef PLATFORM_FMC_IO_H
#define PLATFORM_FMC_IO_H

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAT_FMC_IO_GPRS_TERM_ON = 0,
    PLAT_FMC_IO_GPRS_RESET,
    PLAT_FMC_IO_NRF24L01_CE,
    PLAT_FMC_IO_NRF905_TX_EN,
    PLAT_FMC_IO_NRF905_TRX_CE,
    PLAT_FMC_IO_NRF905_PWR_UP,
    PLAT_FMC_IO_ESP8266_G0,
    PLAT_FMC_IO_ESP8266_G2,
    PLAT_FMC_IO_LED1,
    PLAT_FMC_IO_LED2,
    PLAT_FMC_IO_LED3,
    PLAT_FMC_IO_LED4,
    PLAT_FMC_IO_TP_NRST,
    PLAT_FMC_IO_AD7606_OS0,
    PLAT_FMC_IO_AD7606_OS1,
    PLAT_FMC_IO_AD7606_OS2,
    PLAT_FMC_IO_Y50_0,
    PLAT_FMC_IO_Y50_1,
    PLAT_FMC_IO_Y50_2,
    PLAT_FMC_IO_Y50_3,
    PLAT_FMC_IO_Y50_4,
    PLAT_FMC_IO_Y50_5,
    PLAT_FMC_IO_Y50_6,
    PLAT_FMC_IO_Y50_7,
    PLAT_FMC_IO_AD7606_RESET,
    PLAT_FMC_IO_AD7606_RANGE,
    PLAT_FMC_IO_Y33_2,
    PLAT_FMC_IO_Y33_3,
    PLAT_FMC_IO_Y33_4,
    PLAT_FMC_IO_Y33_5,
    PLAT_FMC_IO_Y33_6,
    PLAT_FMC_IO_Y33_7,
    PLAT_FMC_IO_COUNT
} Plat_FmcIoPin_t;

#define PLAT_FMC_IO_PIN_MASK(pin) (1UL << (uint32_t) (pin))

Plat_Status_t platform_fmc_io_init(void);
bool platform_fmc_io_is_ready(void);
Plat_Status_t platform_fmc_io_write_port(uint32_t value);
uint32_t platform_fmc_io_get_shadow_state(void);
Plat_Status_t platform_fmc_io_write_pin(Plat_FmcIoPin_t pin, bool level);
bool platform_fmc_io_read_pin(Plat_FmcIoPin_t pin);
Plat_Status_t platform_fmc_io_toggle_pin(Plat_FmcIoPin_t pin);

#ifdef __cplusplus
}
#endif

#endif
