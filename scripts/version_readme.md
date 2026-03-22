# Firmware Versioning Notes

## Overview

The versioning flow is now centered on one fixed `96`-byte struct:

- the same struct is embedded into the raw firmware image
- the same struct is exported as `<target>.version.bin`
- the same struct is prepended to `<target>.versioned.bin`

This removes the previous `64-byte runtime struct + 96-byte package struct` split and reduces duplicate field definitions.

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

## Runtime Object

`Core/Src/fw_version.c` exposes:

```c
extern const fw_version_info_t g_fw_version_info;
const fw_version_info_t *fw_version_get(void);
```

The object is linked into `.fw_version`, after `.isr_vector` and before `.text`.

## Build Flow

### 1. Generate stage

`scripts/prepend_version_header.py generate` creates only:

- `build/<config>/generated/fw_version_generated.h`

It writes string fields plus placeholder values:

- `PROJECT_MAGIC_HEAD = "FWVH"`
- `PROJECT_RAW_BIN_CRC32 = 0`
- `PROJECT_WRITE_ADDRESS = FLASH ORIGIN`
- `PROJECT_VALID_BIN_SIZE = 0`

It also emits:

- `FW_VERSION_INFO_INITIALIZER`

So `fw_version.c` no longer repeats the field-by-field initializer.

### 2. Pack stage

`scripts/prepend_version_header.py pack` then:

1. parses `g_fw_version_info` address from `<target>.map`
2. locates the struct inside the raw `<target>.bin`
3. rewrites `write_address` and `valid_bin_size`
4. computes CRC over the whole raw bin with `raw_bin_crc32` temporarily cleared to zero
5. patches the final struct back into the raw bin
6. exports the same 96-byte blob as `<target>.version.bin`
7. writes `<target>.versioned.bin = version.bin + bin`
8. rewrites `fw_version_generated.h` with the final numeric values for inspection

## CRC Rule

Because the CRC field lives inside the same struct that is embedded in the raw bin, the final rule is:

- `raw_bin_crc32` = CRC32 of the whole raw `<target>.bin`
- during calculation, the `raw_bin_crc32` field itself is treated as `0`

Other fields in the struct already contain their final values during CRC calculation.

The CRC algorithm matches STM32 default hardware CRC settings:

- polynomial `0x04C11DB7`
- init `0xFFFFFFFF`
- no inversion
- little-endian 32-bit word grouping

## Outputs

After build:

- `<target>.bin`
  Raw firmware binary, with the final `fw_version_info_t` already patched inside.
- `<target>.version.bin`
  Exact 96-byte copy of the embedded struct.
- `<target>.versioned.bin`
  `version.bin + bin`

## Maintenance

When changing the layout, keep these aligned:

- `Core/Inc/fw_version.h`
- `scripts/prepend_version_header.py`

The script is now the single place that packs, unpacks, patches, and exports the struct, which avoids keeping separate runtime and package layouts in sync.
