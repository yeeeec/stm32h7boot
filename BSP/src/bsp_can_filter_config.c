#include "bsp/bsp_can.h"

#include "fdcan.h"

/* The ICD is the source of message IDs. Until it is supplied, accept every
 * standard frame and let Communication perform the protocol-level validation. */
firmware_status_t BSP_CAN_ConfigFilters(void)
{
    FDCAN_FilterTypeDef filter = {0};

    filter.IdType       = FDCAN_STANDARD_ID;
    filter.FilterIndex  = 0U;
    filter.FilterType   = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1    = 0U;
    filter.FilterID2    = 0U;

    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK)
    {
        return FIRMWARE_STATUS_IO_ERROR;
    }
    return FIRMWARE_STATUS_OK;
}
