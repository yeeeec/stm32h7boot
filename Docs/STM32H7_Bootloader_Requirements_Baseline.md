# STM32H7 HMI Bootloader Requirements Baseline

Document status: Final baseline  
Target: STM32H743 HMI Bootloader  
Storage: SD/FatFs, W25Q256, AT24 EEPROM  
External target: Therapy MCU  
Related protocol: STM32H7_Bootloader_Update_Request_Protocol.md

## 1. Objective

This document defines the complete Bootloader requirements for:

- normal startup;
- Production update using persistent EEPROM Journal;
- Debug update that bypasses EEPROM;
- strict firmware Package validation;
- Manifest-driven installation of executable images and resources;
- SHA-256 integrity verification;
- APP trial confirmation;
- interrupted-install recovery;
- Rollback to CURRENT;
- CURRENT Package commit;
- Runtime validation and Jump;
- Application, Services, Platform, BSP, and Drivers boundaries.

The design shall use only the existing formal request:

~~~text
/UPDATE/boot_update_request.json
~~~

The Debug behavior shall be selected by a Bootloader compile-time macro. No additional file shall be introduced for mode selection or update triggering.

## 2. Frozen system decisions

The following requirements are frozen:

1. /UPDATE/firmware is the primary installation Package.
2. /UPDATE/boot_update_request.json is the only request file.
3. The final /UPDATE directory contains only firmware and the formal request.
4. Production mode initializes EEPROM and uses the Journal state machine.
5. Debug mode bypasses EEPROM and checks the formal request directly.
6. The mode is selected at Bootloader compile time.
7. Both modes reuse the same request parser, Manifest parser, Package validator, component registry, Installer, CURRENT store, and Runtime verifier.
8. The Package is not limited to HMI APP, GUI, and Therapy.
9. Current version 1 components include app, gui, therapy, voice, and config.
10. Future resources are added through the component registry.
11. Bootloader performs SHA-256 integrity checking only.
12. Bootloader does not perform ECDSA, RSA, AES, signature verification, encryption, or decryption.
13. The running Application verifies Package signature before staging.
14. Production mode requires APP confirmation before accepting Candidate as CURRENT.
15. Debug mode accepts a successfully installed and verified Package without APP confirmation.
16. CURRENT_NEW and CURRENT_PREVIOUS are not used.
17. Retained RAM and .noinit request mailboxes are not used.
18. FAILED is a stable state and shall not create an endless Jump/watchdog loop.

## 3. Bootloader build configuration

### 3.1 Required macro

The Bootloader shall define:

~~~c
#ifndef BOOTLOADER_UPDATE_USE_JOURNAL
#define BOOTLOADER_UPDATE_USE_JOURNAL 1U
#endif
~~~

Configuration:

| Value | Intended build | Behavior |
|---:|---|---|
| 1U | Production | Use EEPROM Journal and APP trial confirmation |
| 0U | Debug or factory | Skip EEPROM and inspect the formal request immediately |

Recommended file:

~~~text
Application/Inc/bootloader_config.h
~~~

The word Application in this path means the Bootloader top-level Application layer, not the HMI runtime APP.

### 3.2 Build validation

~~~c
#if (BOOTLOADER_UPDATE_USE_JOURNAL != 0U) && \
    (BOOTLOADER_UPDATE_USE_JOURNAL != 1U)
#error "BOOTLOADER_UPDATE_USE_JOURNAL must be 0U or 1U"
#endif
~~~

### 3.3 Macro ownership

The macro belongs to the Bootloader Application layer.

It shall not be:

- stored in EEPROM;
- read from the request;
- inferred from directory contents;
- interpreted by Platform;
- exposed as a BSP board feature;
- changed during one boot;
- automatically switched after an error.

### 3.4 Build expectations

Production and Debug builds may eliminate unused code through compile-time conditions.

However, they shall not contain separate implementations of:

- request parsing;
- Manifest parsing;
- Package validation;
- component dispatch;
- installation;
- readback;
- SHA-256;
- CURRENT commit;
- Runtime Jump.

## 4. Scope

### 4.1 Bootloader responsibilities

Bootloader shall:

- initialize the minimum hardware required for startup;
- select the build flow using the compile-time macro;
- manage Journal in Production mode;
- avoid all EEPROM access in Debug mode;
- mount and unmount update storage;
- read and validate the formal request;
- strictly parse Manifest;
- calculate the canonical Manifest digest;
- validate product and hardware target;
- validate the exact firmware file set;
- validate every component size and SHA-256;
- install every declared component;
- verify every target write using readback;
- recover from interrupted installation;
- trial-run Candidate and Rollback in Production mode;
- commit an accepted Candidate to CURRENT;
- commit a Debug installation to CURRENT before closing the request;
- validate Runtime vectors;
- Jump to APP;
- service watchdog during bounded long operations;
- report specific failure reasons.

### 4.2 Running Application responsibilities

In Production staging, the running HMI Application shall:

1. read the external firmware Package;
2. strictly parse Manifest;
3. verify the Manifest signature;
4. validate the source file set;
5. validate every source component size and SHA-256;
6. stage the complete Package to /UPDATE/firmware;
7. reread and verify the staged Package;
8. create /UPDATE/boot_update_request.json last;
9. sync, close, reread, and validate the request;
10. commit EEPROM Journal REQUESTED;
11. reset the device.

During Production trial confirmation, the new APP shall:

1. initialize only the EEPROM capability required for confirmation;
2. read JUMPING state;
3. verify running identity;
4. commit confirmation;
5. read back Journal;
6. immediately perform a software reset;
7. avoid full business initialization before that reset.

The HMI Application is not involved in Debug update completion.

### 4.3 Excluded responsibilities

Bootloader shall not:

- discover or download a Package from USB;
- verify ECDSA or RSA signatures;
- decrypt firmware;
- provide APP business functions;
- update the Bootloader itself;
- silently skip unsupported resources;
- use CRC32 as Package integrity;
- use Platform as an update-policy layer.

## 5. Architecture and dependency direction

Required dependency direction:

~~~text
Bootloader Application
        ↓
Services
        ↓
Platform
        ↓
BSP / Drivers
        ↓
HAL / CMSIS / FatFs low-level integration
~~~

Reverse dependency is forbidden.

## 6. Layer responsibilities

### 6.1 Bootloader Application layer

The Bootloader Application layer owns:

- bootloader_config.h;
- compile-time mode selection;
- minimum startup ordering;
- UpdateService entry;
- final action dispatch;
- Reset, Jump, Wait, and fatal handling.

It shall not:

- parse JSON;
- parse Manifest;
- enumerate Package components;
- operate FatFs directly;
- erase or program images directly;
- implement Journal A/B record logic;
- copy CURRENT directly.

### 6.2 Services layer

Services owns update business rules:

- UpdateService;
- BootFlow;
- UpdateJournal;
- UpdateRequest;
- ManifestParser;
- PackageReader;
- ComponentRegistry;
- ImageInstaller;
- CurrentStore;
- RuntimeVerifier;
- version policy;
- retry policy;
- Rollback policy;
- request closure.

Services may recognize:

- UPDATE and CURRENT;
- the formal request;
- Manifest fields;
- app, gui, therapy, voice, and config;
- Candidate and Rollback;
- Journal states;
- current_commit_pending.

Services shall not include HAL handles or FatFs private implementation details.

### 6.3 Platform layer

Platform exposes hardware capabilities only:

- filesystem mount and unmount;
- file open, read, write, close, stat, enumerate, unlink, and sync;
- internal Flash erase, program, and read;
- external Flash erase, program, and read;
- EEPROM fixed-address raw read and write;
- Therapy programming transport;
- watchdog;
- reset;
- time;
- cache and interrupt control;
- CPU Jump;
- logging.

Platform shall not recognize:

- boot_update_request.json fields;
- Manifest schema;
- Package ID;
- component_mask;
- component names;
- Candidate;
- Rollback;
- CURRENT transaction;
- Journal state transitions;
- APP confirmation;
- the compile-time mode macro.

### 6.4 BSP

BSP owns:

- board pin mapping;
- peripheral instances;
- chip-select mapping;
- device capacity;
- memory-region mapping;
- board-specific initialization order.

BSP shall not parse requests or implement update policy.

### 6.5 Drivers

Drivers own reusable device protocols:

- AT24 access;
- W25Q access;
- SD interface;
- Therapy ROM or programming protocol;
- other device-level operations.

Drivers shall not depend on Services.

### 6.6 Callback policy

Business flow shall use normal functions, enums, and result types.

Function-pointer ports are allowed only when a reusable device driver genuinely needs hardware transport abstraction. The component registry should use a static descriptor table and enum/switch unless a simpler implementation cannot satisfy the targets.

## 7. Persistent storage layout

The only final layout is:

~~~text
/UPDATE/
├── boot_update_request.json
└── firmware/
    ├── manifest.json
    └── <every Manifest component file>

/CURRENT/
└── firmware/
    ├── manifest.json
    └── <every accepted Package component file>
~~~

### 7.1 UPDATE requirements

- /UPDATE shall contain only firmware and boot_update_request.json.
- /UPDATE/firmware shall contain exactly one manifest.json.
- /UPDATE/firmware shall contain every Manifest component file.
- /UPDATE/firmware shall not contain undeclared files.
- /UPDATE/firmware shall not contain subdirectories.
- file names are case-sensitive.
- the Package becomes immutable when the formal request exists.
- /UPDATE/firmware remains available until the transaction is safely closed.

### 7.2 Request requirements

- The request is a real runtime protocol file.
- The request is generated by the HMI Application after Package validation.
- The request is the last persistent entry created by staging.
- Both Bootloader modes use the same request.
- No additional control file is allowed.
- Request deletion is the completion marker.

### 7.3 CURRENT requirements

CURRENT stores the last accepted complete Package.

It is:

- the Production Rollback source;
- the accepted version record;
- the source used to recover an older Runtime;
- updated after Candidate confirmation;
- updated immediately after Debug installation succeeds.

CURRENT shall never be considered valid until its exact file set, file sizes, and SHA-256 values are verified.

### 7.4 No extra CURRENT directories

The system shall not use additional CURRENT construction or history directories.

Power-loss safety is provided by:

- retaining UPDATE;
- retaining the request;
- retaining current_commit_pending in Production;
- rebuilding CURRENT from the beginning after interruption;
- validating CURRENT before request deletion.

Candidate confirmation means the old CURRENT may be replaced. Retaining an additional accepted history version is outside this baseline.

## 8. Firmware Package

### 8.1 Package definition

A firmware Package consists of:

~~~text
manifest.json
+ every file referenced by manifest.components
~~~

It can contain:

- executable firmware;
- graphical resources;
- audio resources;
- configuration data;
- fonts;
- languages;
- media;
- other registered product resources.

### 8.2 Version 1 component registry

| Component | Bit | Value | Target category |
|---|---:|---:|---|
| app | 0 | 1 | HMI execution region |
| gui | 1 | 2 | GUI resource region |
| therapy | 2 | 4 | Therapy MCU |
| voice | 3 | 8 | Audio and voice resource region |
| config | 4 | 16 | Configuration resource region |

voice is the version 1 key for audio content.

### 8.3 Extensibility

A new component requires a registry entry defining:

- stable key;
- stable mask bit;
- format;
- maximum size;
- target category;
- destination range;
- erase granularity;
- write alignment;
- readback method;
- installation ordering;
- target completion operation;
- minimum compatible Bootloader version.

Recommended maximum component count:

~~~c
#define UPDATE_MANIFEST_MAX_COMPONENTS 16U
~~~

The update state machine shall not require modification when a new component is registered.

### 8.4 Unsupported components

An unsupported component shall:

- fail validation before erase;
- fail the complete Package;
- not be ignored;
- not be copied without installation;
- not be removed from component_mask;
- not allow partial installation.

## 9. component_mask

component_mask is the exact Manifest component-set identity.

~~~text
component_mask = OR of all registered bits declared by Manifest
~~~

Rules:

- request mask shall equal the derived mask;
- Journal mask shall equal the same value when stored;
- all Manifest components shall be installed;
- mask shall not select a subset;
- extra bits fail validation;
- missing bits fail validation;
- duplicate component keys fail validation.

## 10. Cryptographic and integrity boundary

### 10.1 Signature verification

The running HMI Application performs signature verification before staging.

Bootloader does not perform:

- ECDSA;
- RSA;
- AES;
- decryption;
- public-key loading;
- key selection.

### 10.2 Manifest digest

manifest_sha256 is:

~~~text
SHA-256(
    JCS(
        Manifest without signing.signature
    )
)
~~~

The definition is shared by:

- Application staging;
- the formal request;
- Journal Candidate identity;
- Journal running identity;
- APP confirmation;
- CURRENT validation;
- Runtime identity comparison.

Raw manifest.json bytes shall not be used as manifest_sha256.

### 10.3 Component digest

A component sha256 is the SHA-256 of all bytes in its file.

Bootloader shall calculate it:

- during complete Package preflight;
- during installation;
- when verifying CURRENT;
- whenever a source must be revalidated after interruption.

### 10.4 CRC32

CRC32 may protect EEPROM Journal records.

CRC32 shall not:

- authenticate a Package;
- replace component SHA-256;
- replace Manifest digest;
- approve installation.

### 10.5 Debug security limitation

Debug mode bypasses the Application authentication path. It provides integrity checking but not Package origin authentication.

Therefore Debug mode is limited to physically controlled development, factory, service, or recovery use.

If untrusted users can replace SD contents, Debug mode shall not be enabled in a public Production build.

## 11. Formal request schema

Path:

~~~text
/UPDATE/boot_update_request.json
~~~

Schema version 1:

~~~json
{
  "format_version": 1,
  "requested": true,
  "package_id": "hmi-1.3.4+20260812",
  "manifest_sha256": "6c3ab81f4e5e775629a81b6c8d19c8d507f59cfd9f1796c9e15a99f9b7c4b342",
  "component_mask": 31
}
~~~

Required validation:

- format_version is 1;
- requested is true for an active request;
- package_id exactly matches Manifest;
- manifest_sha256 has 64 lowercase hexadecimal characters;
- digest equals the Bootloader calculation;
- component_mask equals the Manifest-derived value;
- all required fields exist;
- duplicate keys are rejected;
- unknown keys are rejected;
- trailing content is rejected;
- input size is bounded;
- UTF-8 and numeric syntax are valid.

The request shall be deleted after successful completion. It shall not be rewritten as an inactive completion record.

## 12. Application staging order

Production staging shall follow:

~~~text
Authenticate source Package
    ↓
Validate exact source file set
    ↓
Validate all source size and SHA-256
    ↓
Remove old formal request
    ↓
Rebuild /UPDATE/firmware
    ↓
Verify staged file set, size, and SHA-256
    ↓
Create the formal request last
    ↓
Sync, close, reread, and validate request
    ↓
Commit Journal REQUESTED
    ↓
Software reset
~~~

Final persistent layout shall contain no second request or trigger file.

A partial or malformed request shall never permit target erase.

## 13. Boot startup

### 13.1 Common early startup

Both builds shall:

1. configure MPU as required;
2. enable the required caches according to project startup policy;
3. initialize HAL and clocks;
4. initialize minimum GPIO and board capabilities;
5. initialize logs;
6. initialize watchdog;
7. enter UpdateService.

Only the capabilities required by the selected path shall be initialized before the update decision.

### 13.2 Production startup

When BOOTLOADER_UPDATE_USE_JOURNAL is 1U:

1. initialize EEPROM;
2. load Journal;
3. validate Journal;
4. dispatch Journal state;
5. mount update storage only when required.

Normal IDLE with no pending commit:

- shall not mount update storage;
- shall not inspect the request;
- shall validate Runtime;
- shall Jump APP.

### 13.3 Debug startup

When BOOTLOADER_UPDATE_USE_JOURNAL is 0U:

1. do not initialize EEPROM;
2. initialize update storage;
3. mount filesystem;
4. inspect the formal request;
5. install only if the request is active and valid.

If the request is absent:

- unmount storage;
- validate Runtime;
- Jump APP.

If the request is malformed or conflicts with the Package:

- do not erase any target;
- report the error;
- remain in Bootloader error handling.

## 14. Journal state model

### 14.1 State enum

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

No Debug-only state shall be added.

### 14.2 State meanings

| State | Meaning |
|---|---|
| IDLE | No active installation or trial |
| REQUESTED | Application committed a Candidate |
| INSTALLING | Candidate or Rollback is being installed |
| JUMPING | Installed Runtime awaits APP confirmation |
| FAILED | Candidate and Rollback paths cannot produce a confirmed APP |

### 14.3 Journal record

The Journal shall include at least:

- magic;
- format_version;
- sequence;
- state;
- source;
- flags;
- install_attempts;
- jump_attempts;
- commit_attempts;
- last_error;
- candidate_version;
- component_mask;
- candidate_manifest_sha256;
- running_manifest_sha256;
- CRC32.

source shall distinguish:

~~~c
UPDATE_SOURCE_NONE
UPDATE_SOURCE_CANDIDATE
UPDATE_SOURCE_ROLLBACK
~~~

flags shall include:

~~~c
UPDATE_FLAG_CURRENT_COMMIT_PENDING
~~~

The pending flag is not a sixth state.

### 14.4 A/B Journal storage

Journal shall use two EEPROM records or an equivalent power-loss-safe scheme.

Write procedure:

1. validate both slots;
2. choose the highest valid sequence;
3. build the next record;
4. write the inactive slot;
5. write CRC;
6. reread;
7. validate the complete new record;
8. only then accept the new sequence.

Journal writes shall never update the only valid record in place.

### 14.5 Invalid Journal

If both slots are erased and the device is in defined first-boot state, Production Bootloader may initialize IDLE.

Unexpected corruption shall:

- produce JOURNAL_INVALID;
- not claim update success;
- not erase Runtime;
- enter the configured controlled recovery path.

## 15. Production dispatch priority

Production startup shall process:

1. invalid Journal;
2. IDLE with CURRENT_COMMIT_PENDING;
3. REQUESTED;
4. INSTALLING;
5. JUMPING;
6. FAILED;
7. normal IDLE.

| Condition | Required action |
|---|---|
| IDLE + no pending | Validate Runtime and Jump |
| IDLE + commit pending | Commit Candidate Package to CURRENT |
| REQUESTED | Validate and begin Candidate installation |
| INSTALLING | Restart Candidate or Rollback installation |
| JUMPING | Retry trial or begin Rollback |
| FAILED | Remain in stable failure handling |

A request file alone shall not trigger Production installation.

## 16. Common Package validation

Both builds use the same Package validation service.

### 16.1 Request-to-Manifest checks

Before erase:

1. load the formal request;
2. strictly parse Manifest;
3. compare package_id;
4. calculate and compare manifest_sha256;
5. derive and compare component_mask;
6. validate target product;
7. validate target hardware;
8. validate minimum Bootloader version;
9. validate release-version policy;
10. validate every registered component;
11. validate all target ranges;
12. validate installation ordering.

Production additionally verifies Journal Candidate identity.

### 16.2 Exact file set

Expected set:

~~~text
{ manifest.json } ∪ { manifest.components[*].file }
~~~

The firmware directory shall reject:

- missing file;
- extra file;
- hidden file;
- incomplete file;
- subdirectory;
- duplicate name;
- case mismatch;
- non-regular object;
- illegal character;
- path traversal;
- component referencing manifest.json;
- two components referencing one file.

### 16.3 Full Package preflight

All components shall pass before the first erase:

- source stat;
- source size;
- complete source SHA-256;
- registry lookup;
- format policy;
- target capacity;
- address range;
- erase range;
- write alignment;
- range overlap check;
- readback availability;
- target session availability.

The Bootloader shall not begin destructive work after validating only the first component.

## 17. Common Image Installer

### 17.1 Installation plan

The Installer receives a validated installation plan from Services.

The plan contains:

- Package source;
- component count;
- ordered component descriptors;
- expected sizes and digests;
- target ranges;
- target-specific options.

### 17.2 Installation order

Installation order shall protect boot safety.

Recommended default:

1. non-executable resources;
2. external Flash resources;
3. Therapy image;
4. HMI APP last.

A component-specific dependency may override the default only through validated registry policy.

### 17.3 Per-component operation

For every component:

1. open source;
2. initialize target session;
3. validate range;
4. erase target;
5. read a bounded source block;
6. update SHA-256;
7. write target;
8. read target block;
9. compare readback;
10. repeat until exact declared size;
11. verify EOF;
12. verify final digest;
13. finalize target;
14. close target;
15. close source.

### 17.4 Failure behavior

On any component failure:

- stop installation;
- store component and operation error;
- do not Jump a partial Runtime;
- keep request and UPDATE;
- Production follows retry or Rollback policy;
- Debug remains in diagnosable error handling;
- do not continue with later components.

### 17.5 Idempotence

Installation shall be restartable:

- restart from the first component;
- erase before rewriting;
- do not depend on a retained RAM offset;
- do not require byte-level resume;
- repeated installation of the same valid Package shall produce the same target contents.

## 18. Production REQUESTED

When Journal is REQUESTED:

1. mount storage;
2. require the formal request;
3. execute common Package validation;
4. validate CURRENT as Rollback source;
5. set source=CANDIDATE;
6. increment install_attempts;
7. persist INSTALLING;
8. begin common installation.

The transition to INSTALLING shall be persisted before the first target erase.

If validation fails before erase:

- Runtime remains unchanged;
- the specific error is stored;
- the system shall not mark the Candidate successful;
- recovery policy may return to the existing APP or remain in Bootloader.

## 19. Production INSTALLING

Starting in INSTALLING means the previous installation was interrupted or failed.

The Bootloader shall:

1. load source;
2. select UPDATE for Candidate or CURRENT for Rollback;
3. validate the complete source Package;
4. increment and persist install_attempts;
5. restart from the first component.

### 19.1 Candidate write failure

For erase, program, readback, file-read, or digest failure:

- remain logically in INSTALLING;
- retry from the start on the next controlled attempt;
- stop after the configured Candidate install limit;
- validate CURRENT;
- switch source to ROLLBACK;
- reinstall CURRENT.

If CURRENT is missing or invalid, enter FAILED.

### 19.2 Rollback write failure

Rollback installation shall retry up to its configured limit.

If Rollback cannot be installed, enter FAILED.

### 19.3 Successful installation

After Candidate or Rollback installation:

1. validate all target results;
2. save running_manifest_sha256;
3. set JUMPING;
4. set jump_attempts=0;
5. persist Journal;
6. unmount storage;
7. Trial Jump.

## 20. Production JUMPING

JUMPING means an installed Runtime has not confirmed successful startup.

### 20.1 Trial attempt persistence

Before each Trial Jump:

1. validate Runtime vectors;
2. increment jump_attempts;
3. persist Journal;
4. shut down Bootloader peripherals;
5. Jump APP.

If APP resets before confirmation, Bootloader sees JUMPING again.

### 20.2 Candidate APP failure

Recommended limit:

~~~c
#define BOOTLOADER_TRIAL_JUMP_MAX_ATTEMPTS 3U
~~~

Behavior:

- attempts below limit: Trial Jump again;
- limit reached: validate CURRENT and begin Rollback installation;
- CURRENT invalid: enter FAILED.

### 20.3 Rollback APP failure

If Rollback APP also fails to confirm after its limit:

- enter FAILED;
- do not alternate forever between Jump and watchdog reset;
- do not repeatedly erase Runtime;
- preserve diagnostic state.

## 21. APP trial confirmation

### 21.1 Early APP path

Before normal APP tasks, UI, communication, storage, and other business modules, the APP shall check the Journal.

If state is JUMPING:

1. initialize EEPROM only;
2. read the latest Journal record;
3. verify running_manifest_sha256;
4. confirm Candidate or Rollback;
5. reread the committed Journal;
6. perform software reset immediately.

The APP does not need to mount SD for confirmation.

### 21.2 Candidate confirmation

Candidate confirmation writes one atomic Journal record:

- state=IDLE;
- source=CANDIDATE;
- CURRENT_COMMIT_PENDING set;
- candidate identity retained;
- running identity retained;
- jump_attempts cleared;
- last_error cleared.

### 21.3 Rollback confirmation

Rollback confirmation writes:

- state=IDLE;
- source=ROLLBACK;
- CURRENT_COMMIT_PENDING clear;
- jump_attempts cleared;
- original CURRENT retained;
- failed Candidate remains identifiable for cleanup.

### 21.4 Identity mismatch

If running APP identity does not equal Journal running_manifest_sha256:

- APP shall not confirm;
- APP shall not clear JUMPING;
- the next Bootloader start follows trial-failure policy.

## 22. Production CURRENT commit

On IDLE with CURRENT_COMMIT_PENDING:

1. mount storage;
2. require the formal request;
3. revalidate request and UPDATE Package;
4. verify Candidate identity equals the confirmed Runtime;
5. remove incomplete CURRENT contents;
6. copy Manifest and every component from UPDATE to CURRENT;
7. sync and close all CURRENT files;
8. enumerate CURRENT;
9. verify exact CURRENT file set;
10. verify every CURRENT file size and SHA-256;
11. delete the formal request;
12. sync the filesystem;
13. clean UPDATE Package;
14. clear CURRENT_COMMIT_PENDING;
15. clear Candidate transaction fields;
16. persist Journal IDLE;
17. unmount storage;
18. validate Runtime;
19. Jump APP.

The formal request and UPDATE shall remain until CURRENT passes validation.

### 22.1 Interrupted CURRENT commit

If power fails:

- Journal remains IDLE with CURRENT_COMMIT_PENDING;
- the formal request remains;
- UPDATE remains the authoritative source;
- incomplete CURRENT is not used for Rollback;
- the next boot rebuilds CURRENT from the start.

No extra construction directory is required.

### 22.2 Rollback cleanup

After Rollback APP confirmation:

- keep the original CURRENT;
- delete the failed Candidate request;
- clean failed Candidate UPDATE;
- clear Candidate transaction fields;
- remain IDLE.

Failure to clean storage shall be recorded but shall not overwrite the recovered CURRENT.

## 23. Debug update flow

This section applies only when:

~~~c
BOOTLOADER_UPDATE_USE_JOURNAL == 0U
~~~

### 23.1 Startup

Debug Bootloader shall:

1. skip EEPROM initialization;
2. mount update storage;
3. inspect /UPDATE/boot_update_request.json.

### 23.2 No active request

If the request is absent or explicitly inactive by protocol policy:

1. unmount storage;
2. validate current Runtime;
3. Jump current APP.

### 23.3 Invalid request

If the request exists but is malformed, unsupported, or inconsistent:

- do not erase;
- do not treat it as a valid update;
- report the error;
- remain in controlled Bootloader handling.

### 23.4 Active request

If the request is valid and active:

1. execute common Package validation;
2. execute common installation for every Manifest component;
3. validate all installed targets;
4. validate Runtime;
5. rebuild CURRENT from UPDATE;
6. validate CURRENT;
7. delete the formal request;
8. sync filesystem;
9. clean completed UPDATE if configured;
10. unmount storage;
11. Jump APP.

### 23.5 Explicitly omitted Debug behavior

Debug shall not:

- initialize AT24;
- read Journal;
- write Journal;
- use REQUESTED;
- use INSTALLING;
- use JUMPING;
- wait for APP confirmation;
- perform a completion reset;
- automatically reinstall old CURRENT after APP failure.

### 23.6 Debug success boundary

Debug considers the update successful after:

- every component is installed;
- every write passes readback;
- every digest matches;
- Runtime vectors are valid;
- CURRENT is complete and valid;
- the formal request is deleted;
- filesystem sync succeeds.

Whether the APP later completes business initialization is outside Debug success determination.

## 24. Debug power-loss behavior

| Interruption | Required next Debug boot behavior |
|---|---|
| During request validation | Validate again; no target erase if invalid |
| During installation | Request remains; reinstall from the beginning |
| After install, before CURRENT validation | Reinstall if needed and rebuild CURRENT |
| During CURRENT rebuild | Request remains; repeat safely |
| After CURRENT validation, before request deletion | Repeat safely |
| After request deletion, before Jump | No request; validate and Jump installed Runtime |

Debug mode has no EEPROM attempt counter.

Repeated Debug failure shall not cause a self-reset storm. The Bootloader shall expose the error and wait for controlled recovery or power cycle.

## 25. Mode convergence

The only mode-specific preconditions are:

~~~text
Production:
EEPROM init → Journal decision → mount storage when required

Debug:
skip EEPROM → mount storage → inspect formal request
~~~

After an active request is accepted:

~~~text
Request parser
    ↓
Manifest parser
    ↓
Package validator
    ↓
Component registry
    ↓
Image Installer
    ↓
Readback and SHA-256
    ↓
Runtime verifier
    ↓
CurrentStore when required
~~~

Production then waits for APP confirmation before CurrentStore commit.

Debug performs CurrentStore commit immediately before request closure.

## 26. Debug-to-Production transition

A completed Debug update shall leave:

- installed Runtime matching the Package;
- CURRENT matching the Package;
- no formal request;
- synchronized storage.

When a Production build is later programmed:

- an erased Journal may be initialized as IDLE;
- a valid IDLE Journal starts normally;
- a valid non-IDLE Journal is processed according to its stored state.

Debug does not inspect EEPROM. Therefore a factory or service transition shall ensure any unrelated stale non-IDLE Journal is erased or normalized before the Production build is used.

## 27. Runtime validation

Before Jump:

- MSP shall be inside an allowed RAM region;
- MSP shall meet alignment requirements;
- Reset_Handler shall be inside the allowed APP execution region;
- Reset_Handler Thumb bit shall be set;
- vector-table address shall meet VTOR alignment;
- vector words shall not be erased values;
- Runtime identity shall match the expected installed Package when available;
- no partial component installation may remain accepted.

Allowed RAM ranges shall reflect the actual linker map, including only configured DTCM, AXI SRAM, D2 SRAM, and SRAM4 regions.

## 28. Jump implementation

Services decides whether Jump is allowed.

Platform performs the CPU transition.

Required sequence shall be project-specific but centrally implemented:

1. stop new work;
2. sync and unmount storage;
3. deinitialize active DMA and peripherals as required;
4. disable SysTick;
5. disable interrupts;
6. clear pending NVIC interrupts;
7. handle cache according to the validated Bootloader/APP memory contract;
8. set VTOR;
9. set MSP;
10. branch to Reset_Handler.

Cache maintenance shall not be added blindly. It must match MPU attributes, enabled caches, dirty memory regions, and the known STM32H7 startup contract.

## 29. Watchdog

Watchdog shall be serviced during:

- directory enumeration;
- Manifest digest calculation;
- full Package hash validation;
- Flash erase;
- Flash programming;
- readback;
- Therapy programming;
- CURRENT copy;
- CURRENT validation.

Requirements:

- service points are progress-bound;
- a stuck operation shall still time out;
- retry counters are persisted before destructive operations or Trial Jump;
- counters saturate and never wrap.

## 30. Retry configuration

Recommended centralized constants:

~~~c
#define BOOTLOADER_CANDIDATE_INSTALL_MAX_ATTEMPTS 3U
#define BOOTLOADER_ROLLBACK_INSTALL_MAX_ATTEMPTS  3U
#define BOOTLOADER_TRIAL_JUMP_MAX_ATTEMPTS        3U
#define BOOTLOADER_CURRENT_COMMIT_MAX_ATTEMPTS    3U
~~~

Product review may change values, but:

- values shall not be scattered across modules;
- Candidate and Rollback attempts shall be distinguishable;
- an attempt shall be persisted before the corresponding risky action;
- reaching a limit shall cause an explicit state transition.

## 31. FAILED behavior

FAILED is a stable Production state.

In FAILED, Bootloader shall:

- preserve Journal;
- preserve CURRENT and UPDATE where possible;
- preserve last_error;
- avoid repeated erase;
- avoid infinite Jump;
- avoid intentional watchdog-reset loops;
- provide logs or service diagnostics;
- wait in a watchdog-safe controlled loop if appropriate;
- leave FAILED only through an explicit service, factory, or reprogramming operation.

Debug errors do not write FAILED because Debug does not use Journal. They use an equivalent non-persistent controlled error path.

## 32. Error model

Minimum errors:

~~~text
JOURNAL_INVALID
JOURNAL_READ_FAILED
JOURNAL_WRITE_FAILED
REQUEST_NOT_FOUND
REQUEST_READ_FAILED
REQUEST_PARSE_FAILED
REQUEST_VERSION_UNSUPPORTED
REQUEST_NOT_ACTIVE
REQUEST_PACKAGE_ID_MISMATCH
REQUEST_MANIFEST_DIGEST_MISMATCH
REQUEST_COMPONENT_MASK_MISMATCH
STORAGE_INIT_FAILED
STORAGE_MOUNT_FAILED
STORAGE_SYNC_FAILED
MANIFEST_PARSE_FAILED
MANIFEST_POLICY_FAILED
TARGET_MISMATCH
VERSION_REJECTED
COMPONENT_UNSUPPORTED
FILE_SET_MISMATCH
COMPONENT_SIZE_MISMATCH
COMPONENT_HASH_MISMATCH
TARGET_RANGE_INVALID
TARGET_OVERLAP
ERASE_FAILED
WRITE_FAILED
READBACK_FAILED
THERAPY_PROGRAM_FAILED
RUNTIME_VECTOR_INVALID
CURRENT_COMMIT_FAILED
REQUEST_CLOSE_FAILED
TRIAL_UNCONFIRMED
ROLLBACK_FAILED
~~~

Each error shall preserve:

- mode;
- Journal state when applicable;
- source;
- Package identity;
- component identity;
- operation phase;
- attempt counts.

## 33. Service interfaces

### 33.1 Top-level interface

Mode is compile-time, so UpdateService does not need a runtime trigger-mode argument.

~~~c
firmware_status_t UpdateService_Init(void);
update_result_t UpdateService_Process(void);
~~~

### 33.2 Compile-time dispatch

~~~c
update_result_t UpdateService_Process(void)
{
#if (BOOTLOADER_UPDATE_USE_JOURNAL == 1U)
    return UpdateService_ProcessJournalBoot();
#else
    return UpdateService_ProcessFileBoot();
#endif
}
~~~

The two branches are small orchestration functions. They do not duplicate common validators or installers.

### 33.3 APP confirmation interface

Production APP-facing interface:

~~~c
firmware_status_t UpdateService_ConfirmRunning(
    const uint8_t running_manifest_sha256[32]);
~~~

The API validates:

- latest Journal;
- state is JUMPING;
- source is Candidate or Rollback;
- digest matches running identity;
- next Journal record is persisted and read back.

### 33.4 Result enum

Recommended top-level outcomes:

~~~c
typedef enum
{
    UPDATE_RESULT_JUMP_APP = 0,
    UPDATE_RESULT_RESET,
    UPDATE_RESULT_WAIT,
    UPDATE_RESULT_FAILED
} update_result_t;
~~~

Bootloader Application performs the final action.

## 34. Recommended module layout

~~~text
Application/
├── Inc/
│   └── bootloader_config.h
└── Src/
    └── bootloader_main.c

Services/
├── BootFlow/
├── UpdateService/
├── UpdateJournal/
├── UpdateRequest/
├── ManifestParser/
├── PackageReader/
├── ComponentRegistry/
├── ImageInstaller/
├── CurrentStore/
└── RuntimeVerifier/

Platform/
├── Storage/
├── Flash/
├── Eeprom/
├── TherapyProgrammer/
├── Watchdog/
├── System/
└── Log/

BSP/
└── Board instances and memory mapping

Drivers/
└── Reusable device protocols
~~~

Module contracts:

- UpdateRequest parses only the formal request.
- ManifestParser owns schema and JCS digest.
- PackageReader owns source directory and file validation.
- ComponentRegistry maps business components to target categories.
- ImageInstaller owns the common write/readback pipeline.
- CurrentStore owns UPDATE-to-CURRENT copy and validation.
- UpdateJournal owns persistent A/B records only.
- UpdateService owns mode-specific orchestration and state transitions.
- RuntimeVerifier owns Runtime admission checks.
- Platform exposes only physical capabilities.

## 35. Memory requirements

- no unbounded JSON allocation;
- no full component loaded into RAM;
- Manifest size has a fixed limit;
- request size has a fixed limit;
- component count is bounded;
- path and file-name lengths are bounded;
- one shared I/O block buffer is preferred;
- SHA-256 is streamed;
- large update contexts use static memory or a controlled memory region;
- stack usage shall be measured;
- no large manifest object shall be created on a small task stack.

## 36. Filesystem requirements

- every return value is checked;
- short reads and writes are errors;
- close and sync failures are errors;
- exact file count is verified;
- path traversal is rejected;
- directory objects are rejected inside firmware;
- file handles are closed on every exit path;
- filesystem is unmounted before Jump;
- request deletion is followed by sync;
- completed Package data is immutable while request exists.

## 37. Version policy

Services shall validate:

- target product;
- hardware identifier;
- minimum Bootloader version;
- release version format;
- downgrade policy;
- reinstall policy;
- Package ID consistency.

Debug may allow downgrade only through a compile-time product policy. The request itself shall not enable downgrade.

## 38. Testability

Logic shall be host-testable with fake Platform capabilities.

Host tests shall cover:

- request strict parsing;
- JCS digest;
- Manifest strict parsing;
- component_mask derivation;
- exact file-set comparison;
- component registry lookup;
- target-range validation;
- Journal A/B selection;
- state transitions;
- retry limits;
- Candidate confirmation;
- Rollback confirmation;
- CURRENT interruption recovery;
- Debug compile-time branch.

Board tests shall cover:

- real SD;
- real EEPROM;
- internal Flash;
- external Flash;
- Therapy programming;
- watchdog;
- reset reason;
- Runtime Jump;
- power removal at controlled points.

## 39. Acceptance tests

### 39.1 Layout tests

- UPDATE contains only firmware and the formal request.
- Adding another UPDATE-root file is rejected; it never becomes a trigger.
- firmware with an extra file is rejected.
- firmware with a missing file is rejected.
- firmware with a subdirectory is rejected.
- file-name case mismatch is rejected.
- request path is identical in both builds.

### 39.2 Component tests

- app-only Package is handled if product policy allows it.
- app plus gui Package derives mask 3.
- all five current components derive mask 31.
- voice installs as audio resource.
- config installs through its registered target.
- registered future resource installs without changing Journal states.
- unregistered resource is rejected before erase.
- mask cannot omit a declared resource.

### 39.3 Integrity tests

- modified request digest is rejected.
- modified Manifest signed fields change JCS digest.
- modified component content is rejected.
- same size with different content is rejected.
- readback mismatch is rejected.
- raw Manifest file hash is not confused with manifest_sha256.

### 39.4 Production tests

- IDLE does not mount SD.
- IDLE does not install from request alone.
- REQUESTED requires the formal request.
- invalid request causes no erase.
- INSTALLING restarts from the first component after power loss.
- Candidate write failure retries.
- Candidate retry exhaustion starts CURRENT Rollback.
- successful Candidate enters JUMPING.
- jump_attempts is stored before each Trial Jump.
- three failed Candidate trials start Rollback.
- APP confirms using only EEPROM and resets.
- Candidate confirmation sets CURRENT_COMMIT_PENDING.
- interrupted CURRENT commit restarts from UPDATE.
- CURRENT validation completes before request deletion.
- Rollback confirmation preserves old CURRENT.
- Rollback failure reaches FAILED.
- FAILED does not endlessly Jump.

### 39.5 Debug tests

- EEPROM initialization function is never called.
- EEPROM read function is never called.
- EEPROM write function is never called.
- missing request starts current APP.
- malformed request causes no erase.
- valid request installs every Manifest component.
- interrupted install repeats safely.
- CURRENT is committed before request deletion.
- successful Debug update performs no completion reset.
- APP confirmation is not expected.
- request deletion prevents repeated installation.
- later Production build sees consistent CURRENT.

### 39.6 Layer-boundary tests

- Platform has no request or Manifest include.
- Platform has no component name table.
- BSP and Drivers do not include Services headers.
- Bootloader Application does not call FatFs directly.
- mode macro is absent from Platform, BSP, and Drivers.
- component registry exists only in Services.
- only one request parser is compiled.
- only one Installer is compiled.

## 40. Power-loss test matrix

Power shall be removed during:

- Application Package staging;
- request write;
- Journal REQUESTED commit;
- Candidate erase;
- Candidate program;
- Candidate readback;
- Therapy programming;
- transition to JUMPING;
- before Trial Jump;
- APP confirmation Journal write;
- reset after APP confirmation;
- CURRENT copy;
- CURRENT validation;
- request deletion;
- filesystem sync;
- Debug installation;
- Debug CURRENT commit;
- immediately before Jump.

For every point, the next boot shall produce one defined result:

- no destructive action;
- restart Candidate;
- retry Trial;
- begin Rollback;
- restart CURRENT commit;
- start accepted Runtime;
- enter controlled failure.

Undefined partial acceptance is forbidden.

## 41. Implementation migration requirements

The current project shall be changed to:

1. add BOOTLOADER_UPDATE_USE_JOURNAL;
2. remove runtime update-mode selection;
3. branch only in Bootloader startup orchestration;
4. keep one formal request path;
5. remove any assumption of an additional trigger file;
6. implement one strict request parser;
7. align manifest_sha256 with JCS excluding signature;
8. expand component capacity to at least 16;
9. register app, gui, therapy, voice, and config;
10. make component_mask describe the complete Manifest set;
11. preflight every component before erase;
12. share one Installer between both builds;
13. persist INSTALLING before erase;
14. persist jump_attempts before Trial Jump;
15. implement APP early EEPROM confirmation;
16. implement CURRENT_COMMIT_PENDING;
17. rebuild and verify CURRENT before request closure;
18. compile Debug update flow without EEPROM access;
19. add specific error propagation;
20. add host, board, and power-loss tests;
21. update sample request and Manifest so Package ID, digest, and mask match.

## 42. Final state summary

### Production successful Candidate

~~~text
Application authenticates and stages Package
    ↓
Application writes formal request
    ↓
Application writes Journal REQUESTED and resets
    ↓
Bootloader writes INSTALLING and installs UPDATE
    ↓
Bootloader writes JUMPING and Trial Jumps
    ↓
APP confirms through EEPROM and resets
    ↓
Bootloader sees IDLE + CURRENT_COMMIT_PENDING
    ↓
Bootloader rebuilds and validates CURRENT
    ↓
Bootloader deletes formal request
    ↓
Bootloader clears pending state and Jumps APP
~~~

### Production Candidate APP failure

~~~text
Candidate JUMPING reaches attempt limit
    ↓
Bootloader installs CURRENT
    ↓
Bootloader writes JUMPING for Rollback
    ↓
Rollback APP confirms and resets
    ↓
Bootloader keeps old CURRENT and removes failed Candidate
~~~

### Debug successful update

~~~text
Bootloader build macro bypasses EEPROM
    ↓
Bootloader mounts storage
    ↓
Bootloader checks the existing formal request
    ↓
Common validation and installation
    ↓
Runtime validation
    ↓
CURRENT rebuild and validation
    ↓
Formal request deletion and filesystem sync
    ↓
Direct Jump to APP
~~~

## 43. Completion criteria

The Bootloader implementation is complete only when:

- one compile-time macro selects Production or Debug startup;
- no SD file selects the mode;
- exactly one formal request path exists;
- no additional trigger file is required;
- both modes share validation and installation code;
- the Package supports executable and resource components;
- every Manifest component is installed or the entire Package is rejected;
- Bootloader performs SHA-256 but no signature verification;
- Production recovers interrupted writes and failed APP trials;
- Debug performs no EEPROM operation;
- CURRENT remains a valid recovery Package;
- request closure is power-loss safe;
- Platform remains free of update business policy;
- all acceptance and power-loss tests pass.

This baseline and STM32H7_Bootloader_Update_Request_Protocol.md are the authoritative Bootloader update requirements.
