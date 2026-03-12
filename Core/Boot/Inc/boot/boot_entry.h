#ifndef BOOT_ENTRY_H
#define BOOT_ENTRY_H

#include "boot/boot_context.h"

void boot_entry_init(void);
void boot_entry_process(void);
const boot_context_t *boot_entry_get_context(void);

#endif /* BOOT_ENTRY_H */
