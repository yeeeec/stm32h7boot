#ifndef APPLICATION_H
#define APPLICATION_H

#include "firmware/status.h"

struct runtime_service;

firmware_status_t Application_Configure(struct runtime_service *runtime_service);
firmware_status_t Application_Init(void);
firmware_status_t Application_Process(void);

#endif
