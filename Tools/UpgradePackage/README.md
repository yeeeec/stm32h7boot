# Raw SD Upgrade Package

The current SD package is a Manifest-driven raw-image format. The application
binary has no prepended container header.

```text
/
|-- boot_update_request.json       # create this file last
`-- firmware/
    |-- manifest.json
    |-- hmi.app.bin                # raw image linked at 0x90000000
    |-- hmi.app.reloc.bin          # sorted <offset,u16 type,u16 reserved>
    `-- hmi.gui.bin                # raw GUI/resource image
```

`manifest.json` version 2 describes the source image size, vector entry,
source SHA/CRC, both post-relocation target CRCs, and the relocation table's
size/count/CRC. The table itself is separate so the fixed embedded JSON token
budget is not used for thousands of offsets.

For production builds, link the application with `-Wl,--emit-relocs` and run:

```powershell
python Tools/UpgradePackage/build_upgrade_package.py build `
  --app G:\upgrade\hmi.app.bin `
  --gui G:\upgrade\hmi.gui.bin `
  --elf E:\prjs\cmake\stm32h7app\build\Reloc2\stm32h7app.elf `
  --output G:\
```

The tool validates the raw image, derives only `R_ARM_ABS32` XIP relocations,
computes source and pair-1/pair-2 target CRCs, stages all files, verifies the
package, and creates `boot_update_request.json` last. Signature fields retain
the documented opaque placeholder until a real signing service is connected;
the Bootloader does not verify ECDSA signatures.

Use `verify --root G:\` to validate an already staged package without creating
a request file.
