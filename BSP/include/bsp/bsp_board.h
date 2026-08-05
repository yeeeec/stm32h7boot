/**
 * @file bsp_board.h
 * @brief Static board identity information.
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

/** Board identity reported by the board-support package. */
typedef struct
{
    const char *name; /**< Stable board name, owned by the BSP. */
    uint32_t revision; /**< Board hardware revision number. */
} bsp_board_info_t;

/**
 * @brief Return the immutable board identity.
 *
 * @return Pointer to BSP-owned board information with static lifetime.
 */
const bsp_board_info_t *BSP_BoardInfo(void);

#endif
