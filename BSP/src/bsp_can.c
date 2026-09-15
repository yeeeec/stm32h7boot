#include "bsp/bsp_can.h"

#include <stddef.h>
#include <string.h>

#include "fdcan.h"
#include "lwrb/lwrb.h"

#define BSP_CAN_RX_CAPACITY 32U

static lwrb_t s_rx_ring;
static uint8_t s_rx_storage[(BSP_CAN_RX_CAPACITY * sizeof(can_transport_frame_t)) + 1U];
static volatile can_transport_statistics_t s_statistics;
static volatile can_transport_state_t s_state = CAN_TRANSPORT_STATE_UNINITIALIZED;

extern firmware_status_t BSP_CAN_ConfigFilters(void);

static uint8_t DlcFromHal(uint32_t dlc)
{
    switch (dlc)
    {
        case FDCAN_DLC_BYTES_0:
            return 0U;
        case FDCAN_DLC_BYTES_1:
            return 1U;
        case FDCAN_DLC_BYTES_2:
            return 2U;
        case FDCAN_DLC_BYTES_3:
            return 3U;
        case FDCAN_DLC_BYTES_4:
            return 4U;
        case FDCAN_DLC_BYTES_5:
            return 5U;
        case FDCAN_DLC_BYTES_6:
            return 6U;
        case FDCAN_DLC_BYTES_7:
            return 7U;
        default:
            return CAN_TRANSPORT_MAX_DATA_BYTES;
    }
}

static uint32_t DlcToHal(uint8_t dlc)
{
    static const uint32_t values[] = {FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
                                      FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
                                      FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8};
    return values[dlc <= CAN_TRANSPORT_MAX_DATA_BYTES ? dlc : CAN_TRANSPORT_MAX_DATA_BYTES];
}

firmware_status_t BSP_CAN_Init(void)
{
    if (s_state == CAN_TRANSPORT_STATE_STOPPED)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (s_state != CAN_TRANSPORT_STATE_UNINITIALIZED)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    s_statistics.rx_frames      = 0U;
    s_statistics.tx_frames      = 0U;
    s_statistics.rx_overflow    = 0U;
    s_statistics.bus_off_count  = 0U;
    s_statistics.hw_error_count = 0U;
    s_statistics.tx_busy_count  = 0U;
    s_statistics.rx_read_errors = 0U;
    if (lwrb_init(&s_rx_ring, s_rx_storage, sizeof(s_rx_storage)) == 0U)
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if (!FirmwareStatus_IsOk(BSP_CAN_ConfigFilters()))
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL |
                                           FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_BUS_OFF |
                                           FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE,
                                       0U) != HAL_OK)
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    s_state = CAN_TRANSPORT_STATE_STOPPED;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_CAN_Start(void)
{
    if (s_state == CAN_TRANSPORT_STATE_RUNNING)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (s_state != CAN_TRANSPORT_STATE_STOPPED)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    s_state = CAN_TRANSPORT_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_CAN_Stop(void)
{
    if (s_state == CAN_TRANSPORT_STATE_STOPPED)
    {
        return FIRMWARE_STATUS_OK;
    }
    if (s_state != CAN_TRANSPORT_STATE_RUNNING && s_state != CAN_TRANSPORT_STATE_BUS_OFF)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    if (HAL_FDCAN_Stop(&hfdcan1) != HAL_OK)
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    s_state = CAN_TRANSPORT_STATE_STOPPED;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_CAN_Recover(void)
{
    if (s_state != CAN_TRANSPORT_STATE_BUS_OFF && s_state != CAN_TRANSPORT_STATE_ERROR)
        return s_state == CAN_TRANSPORT_STATE_RUNNING ? FIRMWARE_STATUS_OK
                                                      : FIRMWARE_STATUS_INVALID_STATE;
    if (HAL_FDCAN_Stop(&hfdcan1) != HAL_OK || HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        s_state = CAN_TRANSPORT_STATE_ERROR;
        return FIRMWARE_STATUS_IO_ERROR;
    }
    s_state = CAN_TRANSPORT_STATE_RUNNING;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t BSP_CAN_Send(const can_transport_frame_t *frame)
{
    FDCAN_TxHeaderTypeDef header = {0};

    if (frame == NULL || frame->id > 0x7FFU || frame->dlc > CAN_TRANSPORT_MAX_DATA_BYTES)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    if (s_state != CAN_TRANSPORT_STATE_RUNNING)
    {
        return FIRMWARE_STATUS_INVALID_STATE;
    }
    header.Identifier          = frame->id;
    header.IdType              = FDCAN_STANDARD_ID;
    header.TxFrameType         = FDCAN_DATA_FRAME;
    header.DataLength          = DlcToHal(frame->dlc);
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch       = FDCAN_BRS_OFF;
    header.FDFormat            = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    header.MessageMarker       = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, (uint8_t *) frame->data) != HAL_OK)
    {
        if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0U)
        {
            ++s_statistics.tx_busy_count;
            return FIRMWARE_STATUS_BUSY;
        }
        return FIRMWARE_STATUS_IO_ERROR;
    }
    ++s_statistics.tx_frames;
    return FIRMWARE_STATUS_OK;
}

bool BSP_CAN_Read(can_transport_frame_t *frame)
{
    if (frame == NULL)
    {
        return false;
    }
    if (lwrb_get_full(&s_rx_ring) < sizeof(can_transport_frame_t))
    {
        return false;
    }
    if (lwrb_read(&s_rx_ring, frame, sizeof(can_transport_frame_t)) !=
        sizeof(can_transport_frame_t))
    {
        ++s_statistics.rx_read_errors;
        return false;
    }
    return true;
}

can_transport_state_t BSP_CAN_GetState(void)
{
    return s_state;
}

void BSP_CAN_GetStatistics(can_transport_statistics_t *statistics)
{
    if (statistics != NULL)
    {
        statistics->rx_frames      = s_statistics.rx_frames;
        statistics->tx_frames      = s_statistics.tx_frames;
        statistics->rx_overflow    = s_statistics.rx_overflow;
        statistics->bus_off_count  = s_statistics.bus_off_count;
        statistics->hw_error_count = s_statistics.hw_error_count;
        statistics->tx_busy_count  = s_statistics.tx_busy_count;
        statistics->rx_read_errors = s_statistics.rx_read_errors;
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t rx_fifo0_it)
{
    FDCAN_RxHeaderTypeDef header;
    can_transport_frame_t frame;

    if (hfdcan != &hfdcan1 ||
        (rx_fifo0_it & (FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL |
                        FDCAN_IT_RX_FIFO0_MESSAGE_LOST)) == 0U)
    {
        return;
    }
    if ((rx_fifo0_it & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0U)
    {
        ++s_statistics.hw_error_count;
    }
    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
    {
        (void) memset(&header, 0, sizeof(header));
        (void) memset(&frame, 0, sizeof(frame));
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, frame.data) != HAL_OK)
        {
            ++s_statistics.hw_error_count;
            break;
        }
        frame.id          = header.Identifier;
        frame.dlc         = DlcFromHal(header.DataLength);
        frame.reserved[0] = 0U;
        frame.reserved[1] = 0U;
        if (lwrb_get_free(&s_rx_ring) < sizeof(can_transport_frame_t) ||
            lwrb_write(&s_rx_ring, &frame, sizeof(can_transport_frame_t)) !=
                sizeof(can_transport_frame_t))
        {
            ++s_statistics.rx_overflow;
        }
        else
        {
            ++s_statistics.rx_frames;
        }
    }
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *hfdcan)
{
    if (hfdcan != &hfdcan1)
    {
        return;
    }
    ++s_statistics.hw_error_count;
    s_state = CAN_TRANSPORT_STATE_ERROR;
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t error_status_it)
{
    if (hfdcan != &hfdcan1)
    {
        return;
    }
    ++s_statistics.hw_error_count;
    if ((error_status_it & FDCAN_IT_BUS_OFF) != 0U)
    {
        ++s_statistics.bus_off_count;
        s_state = CAN_TRANSPORT_STATE_BUS_OFF;
    }
}
