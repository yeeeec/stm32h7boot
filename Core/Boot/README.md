# Boot Module

This folder contains a CubeMX-safe bootloader framework skeleton.

## Tree

- `Inc/boot`: public interfaces.
- `Src/app`: boot entry + state machine orchestration.
- `Src/domain`: context model.
- `Src/ports`: hardware adaptation layer.
- `Src/diag`: trace, snapshot, gap detection, porting checks.
- `Src/ui`: visualization-facing state API.

## Integration Rule

Only replace files in `Src/ports` when migrating to another board or middleware stack.
Keep `app/domain/diag/ui` hardware-agnostic.
