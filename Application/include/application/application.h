/**
 * @file application.h
 * @brief Top-level Bootloader policy and orchestration lifecycle.
 */
#ifndef APPLICATION_H
#define APPLICATION_H

#include "firmware/status.h"

/** Load the Active Record and initialize the top-level boot policy. */
firmware_status_t Application_Init(void);

/** Run one bounded top-level transition or one Service step. */
firmware_status_t Application_Process(void);

#endif
