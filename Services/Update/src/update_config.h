#ifndef UPDATE_CONFIG_H
#define UPDATE_CONFIG_H

#define UPDATE_PACKAGE_ROOT     "/UPDATE/firmware"
#define CURRENT_ROOT            "/CURRENT"
#define CURRENT_PACKAGE_ROOT    "/CURRENT/firmware"
#define CURRENT_NEW_ROOT        "/CURRENT_NEW"
#define CURRENT_NEW_PACKAGE_ROOT "/CURRENT_NEW/firmware"
#define CURRENT_PREVIOUS_ROOT   "/CURRENT.previous"
#define CURRENT_PREVIOUS_PACKAGE_ROOT "/CURRENT.previous/firmware"

#define UPDATE_MANIFEST_FILE "manifest.json"
#define UPDATE_APP_FILE      "hmi.app.bin"
#define UPDATE_GUI_FILE      "hmi.gui.bin"
#define UPDATE_THERAPY_FILE  "therapy.app.bin"

#define UPDATE_SUPPORTED_MANIFEST_VERSION 1U
#define UPDATE_IO_BLOCK_SIZE              4096U
#define UPDATE_PATH_MAX                   192U

#define UPDATE_JOURNAL_STORAGE_OFFSET 64U
#define UPDATE_JOURNAL_MAGIC          0x55524A4CUL
#define UPDATE_JOURNAL_FORMAT_VERSION 1U

/* Product builds must replace this fail-closed trust anchor with their
 * provisioned SEC1 uncompressed P-256 public key. */
#define UPDATE_TRUSTED_KEY_ID "production"
#define UPDATE_TRUSTED_PUBLIC_KEY_BYTES                                                        \
    {                                                                                           \
        0x04U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,              \
            0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,             \
            0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,             \
            0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U                          \
    }

#endif /* UPDATE_CONFIG_H */
