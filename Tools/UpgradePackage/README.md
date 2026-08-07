# Application Release Package Builder

This independent host tool converts a canonical Application ELF into the fixed
runtime package consumed by this Bootloader. It is not a Bootloader post-build
step and must be run from the Application build once that project exists.

## Input contract

The Application ELF must have file-backed `PT_LOAD` program segments with these
physical load addresses (`p_paddr`):

```text
APP: 0x90000000 .. 0x900FFFFF (maximum 1 MiB)
GUI: 0x90200000 .. 0x909FFFFF (maximum 8 MiB)
```

`p_paddr` is intentionally used instead of `p_vaddr`. This keeps initialized
RAM data whose virtual address is in RAM but whose load address is inside APP
in `hmi.app.bin`. Empty load segments do not contribute bytes. Every non-empty
load range must fit wholly inside exactly one of the fixed regions.

The tool maps APP and GUI addresses to offsets from their respective bases.
Address gaps are filled with `0xFF`. The APP image must contain an eight-byte
vector and a Thumb reset handler pointing into the APP region.

## Dependency

Install the host dependency before building a package:

```powershell
python -m pip install -r Tools/UpgradePackage/requirements.txt
```

## Build a release package

```powershell
python Tools/UpgradePackage/build_upgrade_package.py build `
  --elf E:\build\hmi-application.elf `
  --output E:\release\hmi-1.2.3 `
  --package-id hmi-1.2.3-20260807 `
  --version 1.2.3 `
  --build-number 20260807 `
  --minimum-bootloader 1.0.0
```

The result contains exactly:

```text
<output>/firmware/
  manifest.json
  hmi.app.bin
  hmi.gui.bin
```

The Manifest uses the strict firmware V1 schema: fixed product and hardware,
fixed file names, `raw-bin-v1`, final file sizes, and SHA-256 values computed
from the files that were actually written. The tool stages and self-verifies
all three files before replacing `firmware/`.

## Verify

```powershell
python Tools/UpgradePackage/build_upgrade_package.py verify `
  --package-root E:\release\hmi-1.2.3
```

## Development request only

Normal `build` never generates `boot_update_request.json`. Production release
authentication belongs to the trusted production process.

For explicit development-only testing, generate a request after the final
Manifest is present on disk:

```powershell
python Tools/UpgradePackage/build_upgrade_package.py create-dev-request `
  --package-root E:\release\hmi-1.2.3
```

This command reads the raw `manifest.json` bytes and binds their SHA-256 to the
strict request schema. It prints a development trust override warning and does
not represent production release authentication.

## Rejected input

The tool rejects missing APP or GUI file-backed load data, ranges outside the
fixed regions, ranges crossing a boundary, conflicting overlapping bytes,
invalid vector reset addresses, invalid package identifiers or versions, and
packages whose final files do not match their Manifest.
