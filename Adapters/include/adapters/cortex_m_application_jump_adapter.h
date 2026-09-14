/**
 * @file cortex_m_application_jump_adapter.h
 * @brief Cortex-M7 应用最终交接适配器。
 */
#ifndef ADAPTERS_CORTEX_M_APPLICATION_JUMP_ADAPTER_H
#define ADAPTERS_CORTEX_M_APPLICATION_JUMP_ADAPTER_H

#include "firmware/application_jump.h"

/** 保存无状态 Cortex-M 跳转接口的适配器对象。 */
typedef struct
{
    /** 对外暴露的应用跳转回调表。 */
    application_jump_t interface;
} cortex_m_application_jump_adapter_t;

/** 初始化 Cortex-M 应用跳转适配器。 */
firmware_status_t CortexMApplicationJumpAdapter_Init(cortex_m_application_jump_adapter_t *adapter);

/** 返回适配器持有的应用跳转接口；参数为 NULL 时返回 NULL。 */
const application_jump_t *
CortexMApplicationJumpAdapter_Interface(const cortex_m_application_jump_adapter_t *adapter);

#endif
