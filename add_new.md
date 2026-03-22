#include <stdint.h>

/*==========================================================
 * BootInfo 固定标识
 *==========================================================*/
#define BOOT_INFO_MAGIC     0x5A5AA5A5u

/*==========================================================
 * 槽位定义
 *==========================================================*/
#define SLOT_A              0u          /* App A */
#define SLOT_B              1u          /* App B */
#define SLOT_NONE           0xFFu       /* 无待启动槽位 */

/*==========================================================
 * 确认状态
 *==========================================================*/
#define BOOT_NOT_CONFIRMED  0u          /* 新固件未确认成功 */
#define BOOT_CONFIRMED      1u          /* 新固件已确认成功 */

/*==========================================================
 * 升级状态
 *==========================================================*/
#define UPGRADE_IDLE        0u          /* 空闲，无升级动作 */
#define UPGRADE_READY       1u          /* 新固件已准备好，等待重启测试 */
#define UPGRADE_TESTING     2u          /* 正在试运行新固件 */
#define UPGRADE_ROLLBACK    3u          /* 新固件失败，已回滚 */
#define UPGRADE_SUCCESS     4u          /* 升级成功 */

/*==========================================================
 * 回滚原因
 *==========================================================*/
#define ROLLBACK_NONE           0u      /* 无回滚 */
#define ROLLBACK_CRC_ERROR      1u      /* 镜像CRC错误 */
#define ROLLBACK_BOOT_TIMEOUT   2u      /* 启动超时未确认 */
#define ROLLBACK_BOOT_OVERFLOW  3u      /* 启动次数超限 */
#define ROLLBACK_WDG_RESET      4u      /* 看门狗复位 */
#define ROLLBACK_APP_FAULT      5u      /* App异常故障 */

/*==========================================================
 * s_BootInfo
 * 说明：
 * 1. 保存到 EEPROM（AT24C128）
 * 2. 建议主备各存一份
 * 3. 最后一个 crc 用于校验本结构体自身
 *==========================================================*/
typedef struct
{
    uint32_t magic;
    /*
     * 结构体有效标志，固定写入 BOOT_INFO_MAGIC
     * Bootloader 上电先检查这个值，判断 EEPROM 数据是否有效
     */

    uint8_t  active_slot;
    /*
     * 当前正式运行的槽位
     * 取值：
     *   SLOT_A / SLOT_B
     * 说明：
     *   这是当前“稳定版本”所在分区
     */

    uint8_t  pending_slot;
    /*
     * 下次准备试启动的槽位
     * 取值：
     *   SLOT_A / SLOT_B / SLOT_NONE
     * 说明：
     *   升级后，新固件写到另一个槽位，再把这里设为对应槽位
     */

    uint8_t  confirmed;
    /*
     * 新固件是否已经确认成功
     * 取值：
     *   BOOT_NOT_CONFIRMED / BOOT_CONFIRMED
     * 说明：
     *   新 App 启动稳定后，必须主动写成 BOOT_CONFIRMED
     */

    uint8_t  boot_count;
    /*
     * 待验证固件已经尝试启动的次数
     * 说明：
     *   Bootloader 每次试启动 pending_slot 前先加 1
     *   如果超过 max_boot_count，则回滚
     */

    uint8_t  max_boot_count;
    /*
     * 最大允许试启动次数
     * 建议值：
     *   3
     */

    uint8_t  upgrade_state;
    /*
     * 当前升级状态
     * 取值：
     *   UPGRADE_IDLE
     *   UPGRADE_READY
     *   UPGRADE_TESTING
     *   UPGRADE_ROLLBACK
     *   UPGRADE_SUCCESS
     */

    uint8_t  rollback_reason;
    /*
     * 最近一次回滚原因
     * 取值：
     *   ROLLBACK_NONE
     *   ROLLBACK_CRC_ERROR
     *   ROLLBACK_BOOT_TIMEOUT
     *   ROLLBACK_BOOT_OVERFLOW
     *   ROLLBACK_WDG_RESET
     *   ROLLBACK_APP_FAULT
     */

    uint8_t  reserved0;
    /*
     * 保留字节
     * 用于结构体对齐，或后续扩展
     */

    uint32_t version_a;
    /*
     * App A 当前记录的版本号
     * 建议编码方式：
     *   major.minor.patch -> 0x00MMNNPP
     * 例如：
     *   1.0.5 -> 0x00010005（或你自定义格式）
     */

    uint32_t version_b;
    /*
     * App B 当前记录的版本号
     */

    uint32_t app_a_crc;
    /*
     * App A 镜像 CRC32
     * 说明：
     *   可用于 Bootloader 启动前做镜像合法性校验
     */

    uint32_t app_b_crc;
    /*
     * App B 镜像 CRC32
     */

    uint32_t last_reset_reason;
    /*
     * 最近一次复位原因
     * 说明：
     *   可由 Bootloader 在启动时保存
     *   用于判断是否因 IWDG/WWDG/异常复位导致升级失败
     */

    uint32_t seq;
    /*
     * 写入序号
     * 说明：
     *   主备两份 BootInfo 时，用于判断哪份是最新有效数据
     *   每次保存 BootInfo 时自增
     */

    uint32_t crc;
    /*
     * BootInfo 结构体自身 CRC32
     * 计算范围：
     *   从 magic 开始，到 seq 结束
     *   不包含本 crc 字段自身
     */

} s_BootInfo;