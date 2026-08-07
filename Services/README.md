# Services

Services contains the fixed-runtime update and boot capabilities used by the
Application orchestration layer.

Production dependency direction:

```text
Application -> Services -> Interfaces
Composition -> Application + Services + Adapters
```

The production service set is:

- `boot_control_service`: EEPROM A/B Active Record V2 load and commit.
- `manifest_service`: strict Manifest parsing and binding.
- `update_request_service`: strict trusted-request parsing.
- `update_service`: source verification, fixed APP/GUI Runtime install, and
  candidate Active Record generation.
- `active_validation_service`: fixed APP/GUI SHA and vector validation.
- `launch_service`: fixed Runtime XIP setup and application handoff.
- `runtime_layout`, `vector_validation`, `version_policy`, and checked
  arithmetic capabilities.

Application owns request lifecycle, version policy, commit timing, cleanup,
reset, and the fail-closed decision when no authorized package is available.
Update Service owns all package parsing, source/target hashing, Flash mutation,
and fixed Runtime bounds.
