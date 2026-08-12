/**
 * @file bsp_board.c
 * @brief 静态 Board Identity 实现。
 */
#include "bsp/bsp_board.h"

static const bsp_board_info_t board_info = {
    .name     = "stm32h7boot",
    .revision = 1U,
};

const bsp_board_info_t *BSP_BoardInfo(void)
{
    return &board_info;
}
