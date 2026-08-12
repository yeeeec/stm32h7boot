/**
 * @file version_policy.h
 * @brief 供防回滚策略使用的发布版本排序能力。
 */
#ifndef SERVICES_VERSION_POLICY_H
#define SERVICES_VERSION_POLICY_H

#include "services/common/boot_types.h"

/**
 * @brief 按 major、minor、patch 的优先级比较两个语义版本。
 * @return lhs 小于、等于或大于 rhs 时分别返回负数、零或正数；任一参数为 NULL 时
 *         返回零，调用者应在此之前完成参数校验。
 */
int VersionPolicy_Compare(const release_version_t *lhs, const release_version_t *rhs);

/** @brief 仅在 candidate 严格新于 current 时返回非零。 */
int VersionPolicy_IsUpgrade(const release_version_t *current, const release_version_t *candidate);

#endif
