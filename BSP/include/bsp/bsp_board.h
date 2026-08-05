#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

typedef struct
{
    const char *name;
    uint32_t revision;
} bsp_board_info_t;

const bsp_board_info_t *BSP_BoardInfo(void);

#endif
