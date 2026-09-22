#ifndef APPLICATION_BOOT_FLOW_H
#define APPLICATION_BOOT_FLOW_H

#include <stdint.h>

typedef enum
{
    BOOT_FLOW_LAUNCH = 0,
    BOOT_FLOW_RESET,
    BOOT_FLOW_FATAL
} boot_flow_result_t;

boot_flow_result_t BootFlow_Run(uint32_t *vector_address);

#endif /* APPLICATION_BOOT_FLOW_H */
