/**
 * @file crc32_iso_hdlc.c
 * @brief Table-free CRC-32/ISO-HDLC implementation.
 */
#include "checksum/crc32_iso_hdlc.h"

#include <stddef.h>

#define CRC32_INITIAL_VALUE        0xFFFFFFFFUL
#define CRC32_REFLECTED_POLYNOMIAL 0xEDB88320UL
#define CRC32_OUTPUT_XOR           0xFFFFFFFFUL

static firmware_status_t Reset(void *context)
{
    crc32_iso_hdlc_t *checksum = (crc32_iso_hdlc_t *)context;

    if (checksum == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    checksum->state = CRC32_INITIAL_VALUE;
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t Update(void *context, const void *data, size_t size)
{
    crc32_iso_hdlc_t *checksum = (crc32_iso_hdlc_t *)context;
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    if ((checksum == NULL) || ((data == NULL) && (size != 0U)))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < size; ++index)
    {
        uint32_t bit;

        checksum->state ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (checksum->state & 1U);

            checksum->state =
                (checksum->state >> 1U) ^ (CRC32_REFLECTED_POLYNOMIAL & mask);
        }
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t GetValue(void *context, uint32_t *value)
{
    const crc32_iso_hdlc_t *checksum = (const crc32_iso_hdlc_t *)context;

    if ((checksum == NULL) || (value == NULL))
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }
    *value = checksum->state ^ CRC32_OUTPUT_XOR;
    return FIRMWARE_STATUS_OK;
}

firmware_status_t Crc32IsoHdlc_Init(crc32_iso_hdlc_t *checksum)
{
    if (checksum == NULL)
    {
        return FIRMWARE_STATUS_INVALID_ARGUMENT;
    }

    checksum->interface.context = checksum;
    checksum->interface.reset = Reset;
    checksum->interface.update = Update;
    checksum->interface.get_value = GetValue;
    checksum->state = CRC32_INITIAL_VALUE;
    return FIRMWARE_STATUS_OK;
}

const checksum_t *Crc32IsoHdlc_Interface(const crc32_iso_hdlc_t *checksum)
{
    return (checksum == NULL) ? NULL : &checksum->interface;
}
