/**
 * @file crc32_iso_hdlc.h
 * @brief Incremental CRC-32/ISO-HDLC checksum provider.
 */
#ifndef CHECKSUM_CRC32_ISO_HDLC_H
#define CHECKSUM_CRC32_ISO_HDLC_H

#include <stdint.h>

#include "firmware/checksum.h"

typedef struct
{
    checksum_t interface;
    uint32_t state;
} crc32_iso_hdlc_t;

/** Initialize a reusable CRC-32/ISO-HDLC provider. */
firmware_status_t Crc32IsoHdlc_Init(crc32_iso_hdlc_t *checksum);

/** Return the interface owned by an initialized provider. */
const checksum_t *Crc32IsoHdlc_Interface(const crc32_iso_hdlc_t *checksum);

#endif
