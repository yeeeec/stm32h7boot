/**
 * @file application.h
 * @brief Reserved top-level Bootloader orchestration lifecycle.
 */
#ifndef APPLICATION_H
#define APPLICATION_H

#include "firmware/status.h"

/** Initialize the reserved Application state holder. */
firmware_status_t Application_Init(void);

/** Run one bounded Application iteration; currently no business modes are bound. */
firmware_status_t Application_Process(void);

#endif
