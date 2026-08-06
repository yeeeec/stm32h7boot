/**
 * @file base64.h
 * @brief Strict RFC 4648 Base64 decoder used by signed manifests.
 */
#ifndef SERVICES_BASE64_H
#define SERVICES_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "firmware/status.h"

firmware_status_t Base64_DecodeStrict(
    const char *encoded,
    size_t encoded_size,
    uint8_t *decoded,
    size_t decoded_capacity,
    size_t *decoded_size);

#endif
