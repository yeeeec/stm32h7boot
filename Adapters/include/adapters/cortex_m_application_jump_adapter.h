/**
 * @file cortex_m_application_jump_adapter.h
 * @brief Cortex-M7 final application handoff adapter.
 */
#ifndef ADAPTERS_CORTEX_M_APPLICATION_JUMP_ADAPTER_H
#define ADAPTERS_CORTEX_M_APPLICATION_JUMP_ADAPTER_H

#include "firmware/application_jump.h"

/** Stateless Cortex-M jump interface holder. */
typedef struct
{
    application_jump_t interface;
} cortex_m_application_jump_adapter_t;

/** Initialize a Cortex-M application jump adapter. */
firmware_status_t CortexMApplicationJumpAdapter_Init(
    cortex_m_application_jump_adapter_t *adapter);

/** Return the application-jump interface owned by an adapter. */
const application_jump_t *CortexMApplicationJumpAdapter_Interface(
    const cortex_m_application_jump_adapter_t *adapter);

#endif
