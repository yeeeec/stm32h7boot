/**
 * @file application_jump.h
 * @brief Cortex-M 最终 Application 交接 Contract。
 */
#ifndef FIRMWARE_APPLICATION_JUMP_H
#define FIRMWARE_APPLICATION_JUMP_H

#include <stdint.h>

#include "firmware/status.h"

/**
 * @brief 将控制权转移到已经验证通过的 Application 镜像。
 *
 * @param[in] context Provider 持有的上下文。
 * @param[in] vector_table_address Application 向量表地址。
 * @return 仅在无法启动交接时返回错误。交接成功后不会返回；如果成功后
 *         仍返回，则违反 Contract，必须按 Launch 失败处理。
 */
typedef firmware_status_t (*application_jump_execute_fn)(
    void *context,
    uint32_t vector_table_address);

/**
 * @brief 由 Platform Adapter 持有的最终交接接口。
 *
 * Provider 持有 interface 及其 context；调用者只在 Provider 生命周期内借用。
 * execute() 接受向量表地址后，处理器状态变更由实现负责。
 */
typedef struct
{
    void *context;
    application_jump_execute_fn execute;
} application_jump_t;

#endif
