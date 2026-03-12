#ifndef BOOT_PORTING_H
#define BOOT_PORTING_H

#include <stdint.h>

typedef struct
{
    uint8_t icache_on;
    uint8_t dcache_on;
    uint8_t mpu_on;
    uint32_t vtor;
    uint32_t gap_mask;
} boot_porting_report_t;

void boot_porting_check_run(boot_porting_report_t *report);

#endif /* BOOT_PORTING_H */
