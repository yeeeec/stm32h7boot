/**
 * @file version_policy.c
 * @brief Semantic release-version ordering implementation.
 */
#include "services/capability/version_policy.h"

#include <stddef.h>

static int ComparePart(uint16_t lhs, uint16_t rhs)
{
    if (lhs < rhs)
    {
        return -1;
    }
    if (lhs > rhs)
    {
        return 1;
    }
    return 0;
}

int VersionPolicy_Compare(const release_version_t *lhs, const release_version_t *rhs)
{
    int result;

    if ((lhs == NULL) || (rhs == NULL))
    {
        return 0;
    }

    result = ComparePart(lhs->major, rhs->major);
    if (result == 0)
    {
        result = ComparePart(lhs->minor, rhs->minor);
    }
    if (result == 0)
    {
        result = ComparePart(lhs->patch, rhs->patch);
    }
    return result;
}

int VersionPolicy_IsUpgrade(const release_version_t *current, const release_version_t *candidate)
{
    if ((current == NULL) || (candidate == NULL))
    {
        return 0;
    }

    return VersionPolicy_Compare(candidate, current) > 0;
}
