# Bootloader Porting Checklist

## Build-Time Checks

- `STM32H743xx` macro is defined for the target.
- Linker script defines the bootloader and application memory map clearly.
- `BOOT_CFG_XIP_BASE_ADDR` matches the external flash memory-mapped address.
- CMake includes only one active `boot_ports_*.c` implementation.

## Startup and Core Configuration

- I-Cache and D-Cache are enabled before boot flow runs.
- MPU is enabled and XIP region attributes are configured correctly.
- `SCB->VTOR` is valid before and after app jump.

## USB Upgrade Path

- USB Host stack reaches MSC class ready state.
- FATFS mount and file open work for `BOOT_CFG_UPDATE_FILE_PATH`.
- Read pipeline handles partial and full file reads robustly.

## Image Validation

- Image header format version check.
- Size boundary check against external flash partition.
- CRC32 or signature verification hook.
- Rollback/retry policy for invalid image.

## External Flash and XIP

- Erase/program sequence respects sector alignment.
- Program verification (read-back compare) exists.
- Enter memory-mapped mode after program success.
- Cache invalidate/clean policy around XIP transition.

## Jump-to-App

- Stack pointer from app vector table points to valid SRAM.
- Reset handler address points to executable memory.
- Interrupts are disabled and peripherals are deinitialized as needed.
- `SCB->VTOR` switched to app vector before branch.

## Diagnostics and Visibility

- Stage trace ring buffer is readable in debugger.
- Gap bitmask is exported for quick missing-feature triage.
- Boot stage/progress/error code can be rendered by UI or UART.
