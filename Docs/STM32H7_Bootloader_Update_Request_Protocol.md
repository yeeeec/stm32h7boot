# STM32H7 Bootloader Update Request Protocol

Document status: Final baseline  
Protocol version: 1  
Target: STM32H7 HMI Bootloader  
Related document: STM32H7_Bootloader_Requirements_Baseline.md

## 1. Purpose

This document defines the only firmware update request protocol used by the Bootloader.

The protocol supports two Bootloader build modes:

- Production mode: EEPROM Journal is enabled.
- Debug mode: EEPROM Journal is bypassed.

The mode is selected by a Bootloader compile-time macro. It is not selected by an SD file, Application request, GPIO callback, runtime JSON field, or Platform policy.

Both modes use exactly the same:

- /UPDATE/boot_update_request.json;
- /UPDATE/firmware Package;
- request parser;
- Manifest parser;
- Package validator;
- component registry;
- Image Installer;
- SHA-256 and readback checks;
- CURRENT commit implementation;
- Runtime validation and Jump implementation.

## 2. Frozen decisions

The following decisions are frozen:

1. /UPDATE/firmware is the primary installation Package.
2. /UPDATE/boot_update_request.json is the only update request file.
3. No additional trigger, marker, mode-selection, or completion file is permitted.
4. Production mode uses EEPROM Journal before accessing the update storage.
5. Debug mode does not initialize, read, or write EEPROM.
6. Debug mode mounts update storage and checks the existing request file directly.
7. The mode is selected only by a Bootloader compile-time macro.
8. The firmware Package is Manifest-driven and is not limited to app, gui, and therapy.
9. Bootloader performs SHA-256 integrity verification but does not perform signature verification.
10. Package signature verification is performed by the running Application before it stages the Package.
11. Production mode requires APP trial confirmation.
12. Debug mode accepts a successfully installed Package without APP confirmation.
13. CURRENT_NEW and CURRENT_PREVIOUS directories are not used.
14. Retained RAM and .noinit request mailboxes are not used.

## 3. Bootloader compile-time mode

### 3.1 Configuration macro

The Bootloader shall define one compile-time macro in its own configuration header:

~~~c
#ifndef BOOTLOADER_UPDATE_USE_JOURNAL
#define BOOTLOADER_UPDATE_USE_JOURNAL 1U
#endif
~~~

Meaning:

| Value | Mode | Startup behavior |
|---:|---|---|
| 1U | Production | Initialize EEPROM, load Journal, and run the persistent state machine |
| 0U | Debug | Do not initialize EEPROM; mount storage and check the request file |

Recommended location:

~~~text
Application/Inc/bootloader_config.h
~~~

This is the Bootloader Application layer configuration. The macro shall not be defined or interpreted by Platform, BSP, or Drivers.

### 3.2 Compile-time validation

The configuration header shall reject unsupported values:

~~~c
#if (BOOTLOADER_UPDATE_USE_JOURNAL != 0U) && \
    (BOOTLOADER_UPDATE_USE_JOURNAL != 1U)
#error "BOOTLOADER_UPDATE_USE_JOURNAL must be 0U or 1U"
#endif
~~~

### 3.3 Prohibited mode selection

The mode shall not be selected by:

- another file on SD;
- a JSON field;
- a directory name;
- a file extension;
- retained RAM;
- an EEPROM field when the macro is 0U;
- a Platform capability;
- an automatic fallback from one mode to the other.

## 4. Storage layout

The final persistent layout is exactly:

~~~text
/UPDATE/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    └── <every component file declared by Manifest>

/CURRENT/
└── firmware/
    ├── manifest.json
    └── <every component file of the accepted current Package>
~~~

Rules:

- /UPDATE contains only the firmware directory and boot_update_request.json.
- /UPDATE/firmware contains only manifest.json and files declared by Manifest.
- /CURRENT contains only the firmware directory.
- /CURRENT/firmware contains only manifest.json and files declared by its Manifest.
- File name comparison is case-sensitive.
- Subdirectories inside either firmware directory are forbidden.
- Hidden files, temporary files, editor files, and undeclared files are forbidden in the final Package.
- The request file is outside the firmware directory.
- The request file is not a test file.
- The request file is not copied from USB; the Application generates it after validation and staging.

## 5. Directory semantics

### 5.1 /UPDATE/firmware

/UPDATE/firmware is the authoritative source of the new Package.

It is used for:

- Production Candidate installation;
- Debug installation;
- interrupted installation restart;
- CURRENT reconstruction after Candidate acceptance;
- Debug CURRENT reconstruction before the first Jump.

After boot_update_request.json exists, the contents of /UPDATE/firmware shall remain immutable until the transaction is completed or explicitly discarded.

### 5.2 /CURRENT/firmware

/CURRENT/firmware stores the last accepted complete Package.

In Production mode it is used as:

- Rollback installation source;
- accepted-version identity;
- recovery source after Candidate trial failure.

In Debug mode a successful Package is copied to CURRENT before the request is closed. This ensures a later Production Bootloader sees CURRENT consistent with the installed Runtime.

### 5.3 Request lifetime

The request exists while the update transaction is active.

The transaction is closed only by deleting:

~~~text
/UPDATE/boot_update_request.json
~~~

The request shall not be closed by changing requested to false.

## 6. Firmware Package model

The firmware directory represents one complete update Package.

A Package may contain executable images and non-executable resources. It is not restricted to a fixed three-file layout.

Manifest version 1 currently recognizes:

| Component key | Mask bit | Value | Typical content |
|---|---:|---:|---|
| app | 0 | 1 | HMI Application image |
| gui | 1 | 2 | GUI and TouchGFX resources |
| therapy | 2 | 4 | Therapy MCU image |
| voice | 3 | 8 | Audio, voice, and prompt resources |
| config | 4 | 16 | Configuration and data resources |

voice is the canonical version 1 component key for audio resources.

Future versions may add components such as:

- font;
- language;
- media;
- calibration;
- model;
- other product resources.

A new component must be registered before use.

Recommended parser capacity:

~~~c
#define UPDATE_MANIFEST_MAX_COMPONENTS 16U
~~~

## 7. component_mask semantics

component_mask describes the exact set of components declared by Manifest.

~~~text
component_mask =
    bitwise OR of the stable bit assigned to every Manifest component
~~~

It is not an installation filter.

Examples:

- app + gui = 3;
- app + gui + therapy = 7;
- app + gui + therapy + voice + config = 31.

Requirements:

- request.component_mask shall equal the value derived from Manifest;
- Journal component_mask, when stored, shall equal the same value;
- an extra bit is an error;
- a missing bit is an error;
- every Manifest component shall be installed;
- an unsupported component shall reject the entire Package before any erase.

## 8. Manifest digest

Every protocol field named manifest_sha256 has one definition:

~~~text
SHA-256 of the JCS-normalized Manifest after removing signing.signature
~~~

This definition applies to:

- boot_update_request.json.manifest_sha256;
- Journal candidate_manifest_sha256;
- Journal running_manifest_sha256;
- APP confirmation identity;
- CURRENT Package identity;
- Runtime identity comparisons.

Bootloader shall:

1. read manifest.json;
2. strictly parse the Manifest;
3. remove signing.signature from the digest model;
4. generate JCS bytes;
5. calculate SHA-256;
6. compare it with the request and Journal values.

The raw manifest.json file digest is not manifest_sha256. If an implementation stores it, it shall use a different field name.

Each component sha256 remains the SHA-256 of all raw bytes in that component file.

## 9. Request format

The only request path is:

~~~text
/UPDATE/boot_update_request.json
~~~

Version 1 format:

~~~json
{
  "format_version": 1,
  "requested": true,
  "package_id": "hmi-1.3.4+20260812",
  "manifest_sha256": "6c3ab81f4e5e775629a81b6c8d19c8d507f59cfd9f1796c9e15a99f9b7c4b342",
  "component_mask": 31
}
~~~

Field requirements:

| Field | Requirement |
|---|---|
| format_version | Exactly 1 |
| requested | Exactly true for an active request |
| package_id | Exact match with Manifest |
| manifest_sha256 | 64 lowercase hexadecimal characters |
| component_mask | Exact Manifest-derived mask |

Parser requirements:

- maximum file size shall be fixed; 1024 bytes is recommended;
- the file shall contain one complete JSON object;
- field order may vary;
- duplicate keys are forbidden;
- unknown keys are forbidden;
- missing keys are forbidden;
- trailing non-whitespace data is forbidden;
- invalid UTF-8 is forbidden;
- numeric overflow is forbidden;
- package_id shall follow the Manifest identifier policy;
- uppercase digest characters are rejected;
- requested=false is not an active request.

## 10. Application request creation

The running Application performs Package authentication before staging.

Required order:

1. read the USB firmware Package;
2. strictly parse Manifest;
3. verify the Manifest signature;
4. verify the exact source file set;
5. verify every source component size and SHA-256;
6. remove any old formal request;
7. rebuild /UPDATE/firmware;
8. copy all Manifest files;
9. sync and close all files;
10. reread the staged Package;
11. verify the exact staged file set, size, and SHA-256;
12. write /UPDATE/boot_update_request.json as the final persistent entry;
13. sync and close the request;
14. reread and strictly parse the request;
15. in Production flow, commit Journal REQUESTED;
16. reset the device.

The final /UPDATE directory shall not contain a second control or trigger file.

If power fails while the request is being written:

- Production mode shall not install unless Journal REQUESTED was committed;
- Debug mode shall reject an incomplete or malformed request before erasing any target;
- the Application staging flow shall remove the stale request before a later staging attempt.

## 11. Common request detection

The common request check returns one of:

~~~c
typedef enum
{
    UPDATE_REQUEST_ABSENT = 0,
    UPDATE_REQUEST_ACTIVE,
    UPDATE_REQUEST_INVALID
} update_request_presence_t;
~~~

Meaning:

| Result | Meaning |
|---|---|
| ABSENT | Request file does not exist, or requested is false by defined policy |
| ACTIVE | Request is syntactically valid and requested is true |
| INVALID | File exists but cannot be trusted or parsed |

INVALID shall never be treated as ABSENT after a destructive installation has started.

## 12. Production startup flow

This section applies when:

~~~c
BOOTLOADER_UPDATE_USE_JOURNAL == 1U
~~~

Startup order:

1. initialize minimum Bootloader hardware;
2. initialize watchdog and logging;
3. initialize EEPROM;
4. load and validate Journal;
5. dispatch by Journal state;
6. mount storage only when the state requires Package access.

A normal IDLE boot with no commit pending shall:

- not mount update storage;
- not inspect the request file;
- validate Runtime;
- Jump to the current APP.

A request file alone shall not trigger Production installation.

Journal REQUESTED means the Application has already:

- authenticated the Package;
- staged /UPDATE/firmware;
- written the formal request;
- committed the Journal request.

When Production mode processes REQUESTED, it mounts storage and requires the same formal request and Package validation used by Debug mode.

## 13. Debug startup flow

This section applies when:

~~~c
BOOTLOADER_UPDATE_USE_JOURNAL == 0U
~~~

Startup order:

1. initialize minimum Bootloader hardware;
2. initialize watchdog and logging;
3. do not initialize EEPROM;
4. initialize and mount update storage;
5. check /UPDATE/boot_update_request.json;
6. dispatch by the request check result.

Behavior:

| Request result | Action |
|---|---|
| ABSENT | Unmount storage, validate Runtime, Jump current APP |
| ACTIVE | Execute common Package validation and installation |
| INVALID | Do not erase; report error and remain in Bootloader |

Debug mode shall not:

- read Journal;
- repair Journal;
- clear Journal;
- create Journal;
- enter REQUESTED;
- enter INSTALLING;
- enter JUMPING;
- wait for APP confirmation;
- perform an update-completion reset;
- perform automatic Rollback.

## 14. Common Package validation

Both modes call the same validator before any target erase.

### 14.1 Request and Manifest binding

The validator shall check:

1. request format_version is supported;
2. requested is true;
3. Manifest is strictly valid;
4. request.package_id equals Manifest package_id;
5. request.manifest_sha256 equals the calculated JCS digest;
6. request.component_mask equals the Manifest-derived mask;
7. target.product matches the device;
8. target.hardware matches the device;
9. minimum_bootloader_version is satisfied;
10. release version policy allows installation;
11. every component is registered;
12. every component format is supported;
13. every target range and capacity is valid;
14. no target ranges overlap incorrectly.

Production mode additionally checks that Journal Candidate identity equals the request and Manifest identity.

### 14.2 Exact file set

The exact expected set is:

~~~text
manifest.json + every Manifest component file
~~~

The firmware directory shall reject:

- missing files;
- extra files;
- hidden files;
- incomplete staging files;
- subdirectories;
- duplicate names;
- case mismatch;
- non-regular files;
- path traversal;
- illegal characters.

### 14.3 Full preflight

Before the first erase, the validator shall verify every component:

1. component is registered;
2. source exists;
3. source is a regular file;
4. size equals Manifest size;
5. full source SHA-256 equals Manifest sha256;
6. target exists;
7. target capacity is sufficient;
8. erase range is valid;
9. write alignment is valid;
10. readback is supported;
11. required target-specific session can be opened.

The Bootloader shall not erase one component and discover only later that another Package component is invalid.

## 15. Common installation

After full validation, both modes call the same Installer.

For each component:

1. open the source;
2. open the target session;
3. validate range and alignment again;
4. erase the target;
5. read the source in bounded blocks;
6. update SHA-256;
7. write the target;
8. read back the written block;
9. compare source and readback;
10. verify total byte count;
11. verify final SHA-256;
12. execute any target-specific finalize operation;
13. close the target session;
14. close the source.

Rules:

- every Manifest component shall be installed;
- an unsupported component is a Package error;
- partial installation shall never be accepted;
- APP should be installed last when installation ordering affects boot safety;
- watchdog shall be serviced at bounded progress points;
- installation shall be restartable from the first component;
- cross-reset byte-offset resume is not required.

## 16. Component registry

Services owns a static, versioned component registry.

Recommended model:

~~~c
typedef enum
{
    UPDATE_TARGET_APP = 0,
    UPDATE_TARGET_GUI,
    UPDATE_TARGET_THERAPY,
    UPDATE_TARGET_VOICE,
    UPDATE_TARGET_CONFIG,
    UPDATE_TARGET_RESOURCE
} update_target_t;

typedef struct
{
    const char *name;
    uint32_t mask_bit;
    update_target_t target;
    const char *format;
    uint32_t maximum_size;
} update_component_descriptor_t;
~~~

The registry defines:

- component name;
- stable mask bit;
- allowed format;
- target type;
- maximum size;
- address and capacity limits;
- erase granularity;
- write alignment;
- readback method;
- target-specific completion requirements.

A static table plus enum/switch is preferred. A business callback table is not required.

Services recognizes business component names. Platform exposes only raw storage and programming capabilities.

## 17. Production Journal states

Production mode uses exactly:

~~~c
typedef enum
{
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_REQUESTED,
    UPDATE_STATE_INSTALLING,
    UPDATE_STATE_JUMPING,
    UPDATE_STATE_FAILED
} update_state_t;
~~~

State meanings:

| State | Meaning |
|---|---|
| IDLE | No active installation or trial |
| REQUESTED | Application committed a new Candidate request |
| INSTALLING | Candidate or Rollback installation is active |
| JUMPING | Installed Runtime is awaiting APP confirmation |
| FAILED | Candidate and recovery path cannot produce a confirmed Runtime |

Debug mode does not use these states.

## 18. Production REQUESTED

On REQUESTED:

1. mount storage;
2. read the formal request;
3. execute common Package validation;
4. verify CURRENT as a Rollback source;
5. set install_source=CANDIDATE;
6. increment install_attempts;
7. persist INSTALLING before erase;
8. execute common installation.

If validation fails before erase:

- do not modify Runtime;
- save the specific error;
- follow the configured request-rejection policy;
- do not claim update success.

## 19. Production INSTALLING

If Bootloader starts in INSTALLING, the previous installation was interrupted.

Required behavior:

- load install_source;
- validate the corresponding Package;
- increment and persist install_attempts before erase;
- restart installation from the first component;
- do not resume at a stored byte offset.

Candidate failure handling:

- retry up to the configured limit;
- after the limit, validate CURRENT;
- set install_source=ROLLBACK;
- reinstall CURRENT;
- if CURRENT is invalid, enter FAILED.

Rollback failure handling:

- retry up to the configured limit;
- after the limit, enter FAILED.

## 20. Production JUMPING

After Candidate or Rollback installation succeeds:

1. store running_manifest_sha256;
2. set state=JUMPING;
3. set jump_attempts=0;
4. persist Journal;
5. validate Runtime;
6. increment and persist jump_attempts;
7. Trial Jump.

If the next Bootloader start still sees JUMPING, the APP did not confirm.

Candidate behavior:

- retry Trial Jump while jump_attempts is below the limit;
- after the limit, reinstall CURRENT as Rollback.

Rollback behavior:

- retry Trial Jump while below the limit;
- after the limit, enter FAILED.

Recommended default Jump limit:

~~~c
#define BOOTLOADER_TRIAL_JUMP_MAX_ATTEMPTS 3U
~~~

## 21. APP confirmation

The APP shall check Journal at the earliest safe point.

When Journal is JUMPING, the APP shall:

1. initialize only the EEPROM capability required for confirmation;
2. read Journal;
3. verify running_manifest_sha256 matches the running build identity;
4. submit confirmation;
5. read back and validate the new Journal record;
6. immediately perform a software reset;
7. not continue normal business initialization before the reset.

Candidate confirmation writes:

- state=IDLE;
- current_commit_pending=true;
- jump_attempts=0;
- Candidate identity retained.

Rollback confirmation writes:

- state=IDLE;
- current_commit_pending=false;
- jump_attempts=0;
- original CURRENT retained;
- failed Candidate marked for cleanup.

current_commit_pending is a Journal flag, not a new update state.

## 22. Production CURRENT commit

On startup with IDLE and current_commit_pending=true:

1. mount storage;
2. reread and validate the formal request;
3. revalidate /UPDATE/firmware;
4. verify Candidate identity matches the confirmed Runtime;
5. rebuild /CURRENT/firmware from UPDATE;
6. validate CURRENT exact file set;
7. validate every CURRENT file size and SHA-256;
8. delete the formal request;
9. sync the filesystem;
10. clean /UPDATE/firmware;
11. clear current_commit_pending;
12. clear Candidate transaction fields;
13. persist Journal IDLE;
14. unmount storage;
15. validate Runtime and Jump.

The request and UPDATE Package shall remain until CURRENT validation succeeds.

## 23. Debug successful installation

After common installation succeeds in Debug mode:

1. validate the installed Runtime;
2. rebuild CURRENT from UPDATE;
3. validate CURRENT exact file set, sizes, and SHA-256;
4. delete the formal request;
5. sync the filesystem;
6. clean the completed UPDATE Package if configured;
7. unmount storage;
8. Jump directly to APP.

Debug success does not require:

- APP health confirmation;
- Journal update;
- a second reset;
- automatic Rollback.

The update is considered successful when installation, readback, Runtime validation, CURRENT validation, request deletion, and sync all succeed.

## 24. Request closure and power-loss behavior

### 24.1 Production

| Interruption point | Next startup behavior |
|---|---|
| Before Journal REQUESTED | Production IDLE ignores storage request |
| During Candidate installation | INSTALLING restarts Candidate |
| During Candidate Trial | JUMPING retries or Rollback begins |
| During CURRENT commit | IDLE plus commit pending rebuilds CURRENT |
| After request deletion | Transaction cleanup finishes from Journal state |

### 24.2 Debug

| Interruption point | Next startup behavior |
|---|---|
| Before installation | Request remains; validation runs again |
| During installation | Request remains; installation restarts |
| During CURRENT rebuild | Request remains; installation and commit may repeat |
| After CURRENT validation but before request deletion | Request remains; operation may repeat safely |
| After request deletion but before Jump | No active request; installed Runtime starts |

Debug failure shall not cause a high-speed software-reset loop. It shall remain in a diagnosable Bootloader error state or wait for a controlled power cycle.

## 25. Debug-to-Production transition

After Debug success:

- Runtime matches the accepted Package;
- CURRENT matches the accepted Package;
- the request no longer exists;
- the filesystem is synchronized.

A later Production Bootloader shall:

- initialize an erased or absent Journal as IDLE;
- accept an existing valid IDLE Journal;
- process an existing valid non-IDLE Journal according to its state.

Because Debug mode never accesses EEPROM, factory programming shall ensure that an unrelated stale non-IDLE Journal is erased or normalized before switching to a Production build.

## 26. Layer ownership

### 26.1 Bootloader Application layer

Owns:

- compile-time macro;
- minimum initialization;
- call to UpdateService;
- action on UpdateService result;
- Reset, Wait, or Jump dispatch.

### 26.2 Services

Owns:

- request parsing;
- Manifest parsing;
- JCS digest;
- component registry;
- Package validation;
- Journal state machine;
- installation policy;
- CURRENT transaction;
- Rollback policy;
- Runtime admission decision.

### 26.3 Platform

Owns only capabilities:

- filesystem operations;
- internal and external Flash operations;
- EEPROM raw access;
- Therapy programming;
- watchdog;
- time and reset;
- CPU Jump;
- logs.

Platform shall not recognize:

- request JSON fields;
- Manifest component names;
- component_mask;
- Candidate or Rollback;
- Journal policy;
- CURRENT commit policy;
- build mode macro.

### 26.4 BSP and Drivers

BSP owns board instances and mapping. Drivers own reusable device protocols.

Neither layer shall parse requests, choose modes, or implement update state transitions.

## 27. Recommended interfaces

Mode selection is compile-time and shall not be passed as a runtime configuration object.

Recommended top-level interface:

~~~c
firmware_status_t UpdateService_Init(void);
update_result_t UpdateService_Process(void);
~~~

Internal flow:

~~~c
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
    result = UpdateService_ProcessJournalBoot();
#else
    result = UpdateService_ProcessFileBoot();
#endif
~~~

Both paths shall call the same internal functions after an active request is accepted:

~~~c
UpdateRequest_Load();
PackageReader_Validate();
ImageInstaller_Install();
RuntimeVerifier_Validate();
CurrentStore_Commit();
~~~

Normal-only APP API:

~~~c
firmware_status_t UpdateService_ConfirmRunning(
    const uint8_t running_manifest_sha256[32]);
~~~

The confirmation API shall be unavailable or return INVALID_STATE in a Debug Bootloader build.

## 28. Error classes

At minimum:

- JOURNAL_INVALID;
- JOURNAL_WRITE_FAILED;
- REQUEST_NOT_FOUND;
- REQUEST_READ_FAILED;
- REQUEST_PARSE_FAILED;
- REQUEST_VERSION_UNSUPPORTED;
- REQUEST_NOT_ACTIVE;
- REQUEST_PACKAGE_ID_MISMATCH;
- REQUEST_MANIFEST_DIGEST_MISMATCH;
- REQUEST_COMPONENT_MASK_MISMATCH;
- STORAGE_MOUNT_FAILED;
- MANIFEST_PARSE_FAILED;
- TARGET_MISMATCH;
- VERSION_REJECTED;
- COMPONENT_UNSUPPORTED;
- FILE_SET_MISMATCH;
- COMPONENT_SIZE_MISMATCH;
- COMPONENT_HASH_MISMATCH;
- TARGET_RANGE_INVALID;
- ERASE_FAILED;
- WRITE_FAILED;
- READBACK_FAILED;
- THERAPY_PROGRAM_FAILED;
- CURRENT_COMMIT_FAILED;
- REQUEST_CLOSE_FAILED;
- RUNTIME_VECTOR_INVALID;
- TRIAL_UNCONFIRMED;
- ROLLBACK_FAILED.

Errors shall not be collapsed into one generic I/O result.

## 29. Acceptance requirements

### 29.1 Layout

- The final UPDATE layout contains only firmware and boot_update_request.json.
- No additional control file is required or accepted.
- firmware rejects every undeclared file and subdirectory.
- CURRENT uses only the firmware directory.
- Request path is identical in both modes.

### 29.2 Compile-time mode

- Macro value 1U initializes EEPROM before update storage decisions.
- Macro value 0U never initializes, reads, or writes EEPROM.
- Changing the macro does not compile a second parser or installer.
- SD contents cannot change the selected mode.
- Platform does not reference the macro.

### 29.3 Common validation

- package_id mismatch is rejected.
- JCS digest mismatch is rejected.
- component_mask mismatch is rejected.
- extra or missing component files are rejected.
- unsupported components are rejected before erase.
- app, gui, therapy, voice, and config Package installs successfully.
- a registered future component does not require state-machine changes.

### 29.4 Production

- IDLE does not mount storage or install from a request file alone.
- REQUESTED requires the formal request and Package.
- interrupted INSTALLING restarts from the beginning.
- successful Candidate enters JUMPING.
- APP confirms using EEPROM and immediately resets.
- three unconfirmed Candidate Jumps cause Rollback.
- Candidate confirmation commits CURRENT before closing the request.
- interrupted CURRENT commit recovers using UPDATE.
- failed Rollback ends in stable FAILED.

### 29.5 Debug

- EEPROM functions are not called.
- missing request starts current APP.
- malformed request causes no erase.
- active request installs all Manifest components.
- successful installation commits CURRENT.
- request is deleted only after CURRENT validation.
- APP confirmation is not required.
- completion reset is not performed.
- request removal prevents repeated installation.

## 30. Implementation migration

The implementation shall:

1. add BOOTLOADER_UPDATE_USE_JOURNAL to Bootloader configuration;
2. remove runtime trigger-mode selection;
3. keep exactly one request path;
4. implement one strict request parser;
5. use one common Package validator;
6. use one common Image Installer;
7. use one common CURRENT store;
8. unify manifest_sha256 as JCS excluding signature;
9. expand Manifest component capacity to at least 16;
10. register app, gui, therapy, voice, and config;
11. reject every unsupported component before erase;
12. keep Production Journal recovery and Rollback;
13. compile Debug without any EEPROM dependency in its update flow;
14. commit CURRENT before closing a Debug request;
15. add host, board, and power-loss tests;
16. ensure sample request, Manifest, mask, and digest match exactly.

This protocol is the single source of truth for Bootloader request detection and update-mode selection.
