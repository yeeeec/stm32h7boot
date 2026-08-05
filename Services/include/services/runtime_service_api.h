#ifndef RUNTIME_SERVICE_API_H
#define RUNTIME_SERVICE_API_H

#include "firmware/status.h"

struct runtime_service;

firmware_status_t RuntimeService_Process(struct runtime_service *service);

#endif
