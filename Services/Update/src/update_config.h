#ifndef UPDATE_CONFIG_H
#define UPDATE_CONFIG_H

#include "update/update_request.h"

#define UPDATE_PACKAGE_ROOT  "/UPDATE/firmware"
#define UPDATE_ROOT          "/UPDATE"
#define UPDATE_REQUEST_PATH  UPDATE_REQUEST_FILE_PATH
#define CURRENT_ROOT         "/CURRENT"
#define CURRENT_PACKAGE_ROOT "/CURRENT/firmware"

#define UPDATE_MANIFEST_FILE "manifest.json"

#define UPDATE_SUPPORTED_MANIFEST_VERSION 1U
#define UPDATE_IO_BLOCK_SIZE              4096U
#define UPDATE_PATH_MAX                   192U

#endif /* UPDATE_CONFIG_H */
