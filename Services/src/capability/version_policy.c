/**
 * @file version_policy.c
 * @brief 用于防回滚策略的发布版本比较实现。
 *
 * 发布版本由 major、minor 和 patch 三个无符号字段组成。本文件按该优先级进行
 * 字典序比较，并提供仅接受严格更高版本的升级判定，避免将相同版本或旧版本写入。
 */
#include "services/capability/version_policy.h"

#include <stddef.h>

/**
 * @brief 比较版本号中的一个无符号组成部分。
 *
 * @param lhs 左侧版本组成部分。
 * @param rhs 右侧版本组成部分。
 *
 * @return lhs 小于 rhs 时返回负数；两者相等时返回 0；lhs 大于 rhs 时返回正数。
 *
 * 该函数只比较单个字段，调用者负责依照 major、minor、patch 的优先级决定何时
 * 继续比较下一字段。
 */
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

/**
 * @brief 按 major、minor、patch 的优先级比较两个发布版本。
 *
 * @param lhs 左侧发布版本，不能为 NULL。
 * @param rhs 右侧发布版本，不能为 NULL。
 *
 * @return lhs 较旧时返回负数；版本完全相同时返回 0；lhs 较新时返回正数。
 *         任一参数为 NULL 时也返回 0，调用者须在需要区分无效参数与相等版本时
 *         先自行完成指针校验。
 *
 * 只有当前一层字段相等才继续比较下一层，因此 major 的变化优先于 minor，
 * minor 的变化优先于 patch。
 */
int VersionPolicy_Compare(const release_version_t *lhs, const release_version_t *rhs)
{
    int result;

    if ((lhs == NULL) || (rhs == NULL))
    {
        /* 此比较接口没有状态码；保持历史约定，将无效输入视为不可排序。 */
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

/**
 * @brief 判断候选发布版本是否严格高于当前版本。
 *
 * @param current 当前已激活的发布版本，不能为 NULL。
 * @param candidate 请求安装的候选发布版本，不能为 NULL。
 *
 * @return 1 表示 @p candidate 严格高于 @p current，可以通过版本防回滚检查；
 *         0 表示版本相同、候选版本更旧或任一参数为 NULL。
 *
 * 该函数不接受同版本重装，因为只有 Compare 的结果大于零才被定义为升级。
 */
int VersionPolicy_IsUpgrade(const release_version_t *current, const release_version_t *candidate)
{
    if ((current == NULL) || (candidate == NULL))
    {
        /* 对无法比较的版本采用拒绝升级的保守策略。 */
        return 0;
    }

    return VersionPolicy_Compare(candidate, current) > 0;
}
