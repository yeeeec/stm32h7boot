#ifndef CURRENT_STORE_H
#define CURRENT_STORE_H

#include "firmware/status.h"
#include "update_model.h"

firmware_status_t CurrentStore_Verify(void);
firmware_status_t CurrentStore_VerifyLast(void);
firmware_status_t CurrentStore_Read(update_package_t *package);
firmware_status_t CurrentStore_SaveLast(void);
firmware_status_t CurrentStore_RebuildFrom(const char *source_root);
firmware_status_t CurrentStore_CleanupUpdate(void);

#endif /* CURRENT_STORE_H */
