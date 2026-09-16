/**
 * @file application.c
 * @brief Application 层组合根：绑定 Platform 能力并创建各 RTOS 任务。
 *
 * 本文件只负责依赖注入和任务启动顺序，不实现具体业务；SD、USB 及参数
 * 存储的实际生命周期分别由对应任务拥有。
 */

#include "application/application.h"

firmware_status_t Application_Init(void)
{
    return FIRMWARE_STATUS_OK;
}