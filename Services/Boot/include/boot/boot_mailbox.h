#ifndef BOOT_MAILBOX_H
#define BOOT_MAILBOX_H

#include "firmware/boot_request.h"
#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t UpdateBootMailbox_Write(const BootRequestMessage_t *request);
    firmware_status_t UpdateBootMailbox_Take(BootRequestMessage_t *request);
    void UpdateBootMailbox_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_MAILBOX_H */
