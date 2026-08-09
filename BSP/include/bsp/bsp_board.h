/**
 * @file bsp_board.h
 * @brief 编译目标硬件的不可变身份信息。
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

/** Board Identity；存储和字符串数据由 BSP 持有。 */
typedef struct
{
    const char *name;
    uint32_t revision;
} bsp_board_info_t;

/**
 * @brief 返回不可变的 Board Identity。
 *
 * @return 指向 BSP 持有、静态生命周期只读数据的指针。调用者不得修改或释放
 *         返回对象。
 */
const bsp_board_info_t *BSP_BoardInfo(void);

#endif
