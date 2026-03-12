#ifndef BOOT_FLOW_H
#define BOOT_FLOW_H

#include "boot/boot_context.h"

void boot_flow_init(boot_context_t *ctx);
boot_result_t boot_flow_step(boot_context_t *ctx);

#endif /* BOOT_FLOW_H */
