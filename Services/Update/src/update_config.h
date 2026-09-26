#ifndef UPDATE_CONFIG_H
#define UPDATE_CONFIG_H

#define UPDATE_PACKAGE_ROOT  "/UPDATE/firmware"
#define CURRENT_ROOT         "/CURRENT"
#define CURRENT_PACKAGE_ROOT "/CURRENT/firmware"
#define CURRENT_PACKAGE_PART "/CURRENT/firmware.part"
#define LAST_ROOT            "/LAST"
#define LAST_PACKAGE_ROOT    "/LAST/firmware"
#define LAST_PACKAGE_PART    "/LAST/firmware.part"

#define UPDATE_MANIFEST_FILE "manifest.json"

#define UPDATE_SUPPORTED_MANIFEST_VERSION 1U
#define UPDATE_IO_BLOCK_SIZE              4096U
#define UPDATE_PATH_MAX                   192U

#endif /* UPDATE_CONFIG_H */
