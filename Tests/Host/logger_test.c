#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "firmware/log_sink.h"
#include "firmware/system_clock.h"
#include "logging.h"
#include "logging_setup.h"

static char captured_line[256];
static size_t captured_size;

static firmware_status_t CaptureWrite(
    void *context,
    const uint8_t *data,
    size_t size)
{
    (void)context;
    assert(size <= sizeof(captured_line));
    memcpy(captured_line, data, size);
    captured_size = size;
    return FIRMWARE_STATUS_OK;
}

static uint32_t FixedNowMs(void *context)
{
    (void)context;
    return 3723004U;
}

int main(void)
{
    const log_sink_t sink = {NULL, CaptureWrite};
    const system_clock_t clock = {NULL, FixedNowMs};
    char long_message[256];

    assert(Logging_Configure(NULL, &clock) ==
           FIRMWARE_STATUS_INVALID_ARGUMENT);
    assert(Logging_Configure(&sink, &clock) == FIRMWARE_STATUS_OK);
    assert(Logging_Configure(&sink, &clock) ==
           FIRMWARE_STATUS_INVALID_STATE);

    LOG_INFO("test", "value=%u", 42U);
    assert(captured_size ==
           strlen("[01:02:03.004][INF][test] value=42\r\n"));
    assert(memcmp(captured_line,
                  "[01:02:03.004][INF][test] value=42\r\n",
                  captured_size) == 0);

    memset(long_message, 'x', sizeof(long_message));
    long_message[sizeof(long_message) - 1U] = '\0';
    LOG_DEBUG("test", "%s", long_message);
    assert(captured_size <= 192U);
    assert(captured_size >= 2U);
    assert(captured_line[captured_size - 2U] == '\r');
    assert(captured_line[captured_size - 1U] == '\n');

    return 0;
}
