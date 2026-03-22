#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

void platform_init(void);
void platform_process(void);
bool platform_is_ready(void);
const char *platform_get_name(void);

#ifdef __cplusplus
}
#endif

#endif
