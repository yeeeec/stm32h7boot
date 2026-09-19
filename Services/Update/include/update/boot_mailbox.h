#ifndef FIRMWARE_BOOT_MAILBOX_H
#define FIRMWARE_BOOT_MAILBOX_H

#include "firmware/boot_request.h"
#include "firmware/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    firmware_status_t UpdateBootMailbox_Write(const BootRequestMessage_t *message);
    firmware_status_t UpdateBootMailbox_Take(BootRequestMessage_t *message);
    void UpdateBootMailbox_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_BOOT_MAILBOX_H */
