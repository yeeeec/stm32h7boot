/**
 * @file application.h
 * @brief Bootloader 顶层策略与编排生命周期 API。
 *
 * Application 在裸机主循环中增量驱动更新检查、Runtime 校验和最终启动。
 * 具体存储、解析、安装及跳转操作均委托给 Composition 注入的 Service。
 */
#ifndef APPLICATION_H
#define APPLICATION_H

#include "firmware/status.h"

/**
 * @brief 加载 Active Record 并初始化顶层启动策略。
 *
 * @return FIRMWARE_STATUS_OK 表示初始化完成，可开始调用 Application_Process()。
 * @return FIRMWARE_STATUS_INVALID_STATE 表示尚未配置依赖或已经初始化。
 * @return 其他状态表示 Boot Control 读取发生不可恢复错误。
 *
 * @pre Composition 已成功调用 Application_Configure() 发布完整依赖图。
 */
firmware_status_t Application_Init(void);

/**
 * @brief 推进一步有界的顶层状态转换或一个 Service 步骤。
 *
 * 调用者应在裸机主循环中持续调用本函数。启动交接或系统复位成功时不会返回；
 * 进入失败终态后，后续调用返回 FIRMWARE_STATUS_INVALID_STATE。
 *
 * @return FIRMWARE_STATUS_OK 表示本次步骤已完成且状态机可继续运行。
 * @return FIRMWARE_STATUS_INVALID_STATE 表示尚未初始化或已进入失败终态。
 */
firmware_status_t Application_Process(void);

#endif
