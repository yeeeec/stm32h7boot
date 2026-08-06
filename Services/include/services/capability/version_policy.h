/**
 * @file version_policy.h
 * @brief Release-version ordering used by anti-rollback policy.
 */
#ifndef SERVICES_VERSION_POLICY_H
#define SERVICES_VERSION_POLICY_H

#include "services/common/boot_types.h"

/** Compare semantic release versions, returning less than, equal to, or greater than zero. */
int VersionPolicy_Compare(
    const release_version_t *lhs,
    const release_version_t *rhs);

/** Return nonzero only when candidate is strictly newer than current. */
int VersionPolicy_IsUpgrade(
    const release_version_t *current,
    const release_version_t *candidate);

#endif
