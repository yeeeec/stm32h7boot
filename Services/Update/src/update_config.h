#ifndef UPDATE_CONFIG_H
#define UPDATE_CONFIG_H

#define UPDATE_PACKAGE_ROOT  "/UPDATE/firmware"
#define CURRENT_ROOT         "/CURRENT"
#define CURRENT_PACKAGE_ROOT "/CURRENT/firmware"

#define UPDATE_MANIFEST_FILE "manifest.json"
#define UPDATE_APP_FILE      "hmi.app.bin"
#define UPDATE_GUI_FILE      "hmi.gui.bin"
#define UPDATE_THERAPY_FILE  "therapy.app.bin"

#define UPDATE_SUPPORTED_MANIFEST_VERSION 1U
#define UPDATE_IO_BLOCK_SIZE              4096U
#define UPDATE_PATH_MAX                   192U

#define UPDATE_JOURNAL_STORAGE_OFFSET 64U
#define UPDATE_JOURNAL_SLOT_SIZE      128U

#define UPDATE_MAX_INSTALL_ATTEMPTS 3U
#define UPDATE_MAX_JUMP_ATTEMPTS    3U
#define UPDATE_MAX_COMMIT_ATTEMPTS  3U

#endif /* UPDATE_CONFIG_H */
