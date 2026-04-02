#ifndef BOOT_UDISK_CHECK_H
#define BOOT_UDISK_CHECK_H

#include "boot_error.h"

#ifndef BOOT_UDISK_CHECK_FEATURE_CODE
#define BOOT_UDISK_CHECK_FEATURE_CODE 0x12345678UL
#endif

#ifndef BOOT_UDISK_CHECK_CODE_LITTLE_ENDIAN
#define BOOT_UDISK_CHECK_CODE_LITTLE_ENDIAN 1U
#endif

BootError Boot_Udisk_Check(void);

#endif
