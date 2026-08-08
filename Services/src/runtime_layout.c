/**
 * @file runtime_layout.c
 * @brief 固定 APP 与 GUI Runtime 布局实例。
 *
 * 此定义把头文件中的编译期地址常量封装为只读依赖对象，便于 Service 在不直接
 * 依赖具体 Flash Adapter 的前提下完成边界校验。
 */
#include "services/common/runtime_layout.h"

const boot_runtime_layout_t BOOT_RUNTIME_LAYOUT = {
    /* APP 的 Flash 偏移、最大长度和 XIP 基址必须与发布合同一致。 */
    BOOT_APP_FLASH_OFFSET,
    BOOT_APP_RUNTIME_SIZE,
    BOOT_APP_RUNTIME_BASE,
    /* GUI 紧随保留间隙后的固定区域，同样使用无头 RAW payload。 */
    BOOT_GUI_FLASH_OFFSET,
    BOOT_GUI_RUNTIME_SIZE,
    BOOT_GUI_RUNTIME_BASE,
};

const boot_runtime_layout_t *BootRuntimeLayout_Get(void)
{
    /* 返回单例而非复制结构，保证所有 Service 使用同一固定布局。 */
    return &BOOT_RUNTIME_LAYOUT;
}
