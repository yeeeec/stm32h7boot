#ifndef PLATFORM_DEFINITIONS_H
#define PLATFORM_DEFINITIONS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLAT_OK = 0,
    PLAT_ERR_TIMEOUT = -1,
    PLAT_ERR_BUSY = -2,
    PLAT_ERR_INVALID_PARAM = -3,
    PLAT_ERR_HW_FAILURE = -4
} Plat_Status_t;

#endif
