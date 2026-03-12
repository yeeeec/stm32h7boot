# STM32H7 Bootloader Architecture Skeleton

This project now contains a bootloader-oriented software skeleton under `Core/Boot`.
It is designed to keep CubeMX-generated code stable while allowing custom logic to evolve independently.

## Layered Layout

- `Core/Boot/Inc/boot`
  - Public interfaces and shared model types.
- `Core/Boot/Src/app`
  - Boot use-case orchestration and state machine entry.
- `Core/Boot/Src/domain`
  - Boot context/state data handling.
- `Core/Boot/Src/ports`
  - Hardware/driver adaptation interfaces and stub implementation.
- `Core/Boot/Src/diag`
  - Trace, gap discovery, and portability checks.
- `Core/Boot/Src/ui`
  - Visual status model API (currently independent from LCD implementation).

## Current Boot Flow

- `INIT`
- `WAIT_USB`
- `LOAD_PACKAGE`
- `VERIFY_PACKAGE`
- `PROGRAM_EXT_FLASH`
- `MAP_XIP`
- `JUMP_APP`

All hardware-sensitive operations are abstracted by `boot_ports.h`.
The current `boot_ports_stub.c` intentionally returns `BOOT_RESULT_NOT_IMPLEMENTED`
for non-trivial stages to expose integration gaps early.

## Decoupling Rules

- Core flow logic must not directly call HAL USB/FATFS/QSPI details.
- New boards/chips should only replace `ports` layer implementation.
- Diagnostics and visualization APIs should remain side-effect free and callable from tests.

## Next Integration Targets

- Replace `boot_ports_stub.c` with board implementation:
  - USB MSC package discovery + file loading.
  - App image header/signature/CRC validation.
  - External flash erase/program/verify.
  - QSPI memory-mapped mode entry and cache/MPU synchronization.
  - App vector-table jump.
- Bind `boot_visual` with LTDC display widgets or UART shell.
- Add unit tests for `boot_flow_step()` stage transitions and error policies.
