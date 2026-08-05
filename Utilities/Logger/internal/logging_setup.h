/**
 * @file logging_setup.h
 * @brief Composition-only dependency binding for the logger frontend.
 */
#ifndef FIRMWARE_LOGGING_SETUP_H
#define FIRMWARE_LOGGING_SETUP_H

#include "firmware/log_sink.h"
#include "firmware/status.h"
#include "firmware/system_clock.h"

/** Bind the logger once to a synchronous output sink and monotonic clock. */
firmware_status_t Logging_Configure(
    const log_sink_t *sink,
    const system_clock_t *clock);

#endif
