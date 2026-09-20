# Update Service Boundary

The bootloader update path is now layered as `Application -> Services -> Platform`.

Application initializes the platform, consumes the boot-flow result, resets after
an installation or recovery, and performs the final CPU vector check before
jumping to the application. It does not access storage, manifests, flash,
Therapy, or journal records.

Services consume the retained-RAM `BootRequestMessage_t` mailbox and own package
loading, raw manifest hashing, mandatory signing metadata and ECDSA P-256
verification, exact package file-set checks, version policy, image installation,
CURRENT validation/commit/recovery, and the A/B EEPROM journal.

The former EEPROM `BootControl_t`, SD JSON request, Application callback tables,
Service hardware ports, and duplicate `Services/stm32isp` target are removed.
Services call the concrete `PlatformStorage_*`, `PlatformFlash_*`,
`PlatformTherapy_*`, and `PlatformNvStorage_*` APIs directly.
