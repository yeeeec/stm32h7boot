#ifndef RUNTIME_VERIFIER_H
#define RUNTIME_VERIFIER_H

#include "firmware/status.h"
#include "update_model.h"

firmware_status_t RuntimeVerifier_Validate(void);
firmware_status_t RuntimeVerifier_VerifyPackage(const update_package_t *package);

#endif /* RUNTIME_VERIFIER_H */
