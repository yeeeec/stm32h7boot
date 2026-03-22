#include "boot_log.h"

#include "platform/boot_platform.h"

void Boot_Log_Init(void) {
    logging_register_tick_provider(Boot_Platform_GetTickMs);
    logging_init_module();
}
