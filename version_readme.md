# Firmware Versioning Notes

## Overview

The versioning flow is now split into two parts:

- the raw firmware image keeps only git-related runtime info
- a fixed `96`-byte `fw_version_info_t` package header is exported as `<target>.version.bin`
- the same package header is prepended to `<target>.versioned.bin`

This keeps the application image at its natural runtime base address while still exporting a self-contained package header for update tooling.

## Single Struct Layout

Defined in `Core/Inc/fw_version.h`:

```c
typedef struct __attribute__((packed)) fw_version_info_t {
  char magic[4];
  char project_name[32];
  char image_tag[4];
  char git_hash[8];
  char build_time[20];
  uint32_t raw_bin_crc32;
  uint32_t write_address;
  uint32_t valid_bin_size;
  uint8_t reserved[16];
} fw_version_info_t;
```

Offset summary:

- `0x00` `magic[4]`
- `0x04` `project_name[32]`
- `0x24` `image_tag[4]`
- `0x28` `git_hash[8]`
- `0x30` `build_time[20]`
- `0x44` `raw_bin_crc32`
- `0x48` `write_address`
- `0x4C` `valid_bin_size`
- `0x50` `reserved[16]`

## Runtime Git Info

`Core/Src/fw_version.c` keeps only the git hash inside the raw firmware image.

- the git hash is emitted into `.fw_git_info`
- `fw_version_git_hash()` returns that embedded string
- the raw firmware image does not embed `fw_version_info_t`

## Build Flow

### 1. Generate stage

`scripts/prepend_version_header.py generate` creates only:

- `build/<config>/generated/fw_version_generated.h`

It writes package string fields plus placeholder values:

- `PROJECT_MAGIC_HEAD = "FWVH"`
- `PROJECT_RAW_BIN_CRC32 = 0`
- `PROJECT_WRITE_ADDRESS = FLASH ORIGIN`
- `PROJECT_VALID_BIN_SIZE = 0`

For package struct initialization it also emits matching `*_INIT` byte-array macros, so
fixed-width fields such as `magic[4]` and `image_tag[4]` are initialized without
string-truncation warnings.

It also emits:

- `FW_VERSION_INFO_INITIALIZER`

### 2. Pack stage

`scripts/prepend_version_header.py pack` then:

1. reads package string fields back from `fw_version_generated.h`
2. reads the full raw `<target>.bin` as-is
3. computes CRC over the complete raw bin starting at the runtime FLASH origin
4. builds the final `fw_version_info_t` blob with that CRC
5. exports the 96-byte blob as `<target>.version.bin`
6. writes `<target>.versioned.bin = version.bin + bin`
7. rewrites `fw_version_generated.h` with the final numeric values for inspection

## CRC Rule

Because `fw_version_info_t` is no longer embedded inside the raw bin, the final rule is:

- `raw_bin_crc32` = CRC32 of the whole raw `<target>.bin`
- the CRC is computed directly against the bytes that run at the FLASH origin, for example `0x90000000`

The CRC algorithm matches STM32 default hardware CRC settings:

- polynomial `0x04C11DB7`
- init `0xFFFFFFFF`
- no inversion
- little-endian 32-bit word grouping

## Outputs

After build:

- `<target>.bin`
  Raw firmware binary only. It is not patched with `fw_version_info_t`.
- `<target>.version.bin`
  Exact 96-byte package header built from the generated metadata plus the raw bin CRC.
- `<target>.versioned.bin`
  `version.bin + bin`

## Maintenance

When changing the layout, keep these aligned:

- `Core/Inc/fw_version.h`
- `scripts/prepend_version_header.py`

The script is now the single place that builds and exports the package header, which avoids keeping separate runtime and package layouts in sync.
