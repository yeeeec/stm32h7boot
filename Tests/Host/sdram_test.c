#include <assert.h>
#include <stdint.h>

#include "devices/sdram.h"

typedef struct
{
    sdram_command_t commands[4];
    uint32_t command_count;
    uint32_t auto_refresh_count;
    uint32_t mode_register;
    uint32_t refresh_rate;
    uint32_t delay_ms;
} fake_sdram_port_t;

static firmware_status_t SendCommand(
    void *context,
    sdram_command_t command,
    uint32_t auto_refresh_count,
    uint32_t mode_register)
{
    fake_sdram_port_t *fake = (fake_sdram_port_t *)context;

    assert(fake->command_count < 4U);
    fake->commands[fake->command_count] = command;
    ++fake->command_count;
    if (command == SDRAM_COMMAND_AUTO_REFRESH)
    {
        fake->auto_refresh_count = auto_refresh_count;
    }
    if (command == SDRAM_COMMAND_LOAD_MODE)
    {
        fake->mode_register = mode_register;
    }
    return FIRMWARE_STATUS_OK;
}

static firmware_status_t SetRefreshRate(void *context, uint32_t refresh_rate)
{
    ((fake_sdram_port_t *)context)->refresh_rate = refresh_rate;
    return FIRMWARE_STATUS_OK;
}

static void DelayMs(void *context, uint32_t delay_ms)
{
    ((fake_sdram_port_t *)context)->delay_ms += delay_ms;
}

int main(void)
{
    fake_sdram_port_t fake = {0};
    sdram_t device = {0};
    sdram_port_t port = {
        .context = &fake,
        .send_command = SendCommand,
        .set_refresh_rate = SetRefreshRate,
        .delay_ms = DelayMs,
    };
    sdram_config_t config = {
        .startup_delay_ms = 1U,
        .auto_refresh_count = 8U,
        .mode_register = 0x0230U,
        .refresh_rate = 761U,
    };

    assert(Sdram_Init(&device, &port, &config) == FIRMWARE_STATUS_OK);
    assert(fake.command_count == 4U);
    assert(fake.commands[0] == SDRAM_COMMAND_CLOCK_ENABLE);
    assert(fake.commands[1] == SDRAM_COMMAND_PRECHARGE_ALL);
    assert(fake.commands[2] == SDRAM_COMMAND_AUTO_REFRESH);
    assert(fake.commands[3] == SDRAM_COMMAND_LOAD_MODE);
    assert(fake.auto_refresh_count == 8U);
    assert(fake.mode_register == 0x0230U);
    assert(fake.refresh_rate == 761U);
    assert(fake.delay_ms == 1U);
    return 0;
}
