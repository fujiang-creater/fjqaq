# Fixed SPIFFS Voice Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create an isolated ESP-IDF 6.0.2 project named `robotdogsay` that plays a few fixed WAV phrases from SPIFFS over MAX98357A on ESP32-S3.

**Architecture:** Copy the existing ESP-IDF app into a new standalone project, then replace the servo/web stack with a small audio stack: boot diagnostics, SPIFFS mount, a blocking-free I2S playback task, and a serial command loop that selects fixed clips. Audio files live in SPIFFS as 16 kHz, 16-bit, stereo WAVs; playback duplicates mono sample values into both channels when needed and drains silence at the end to avoid pop noise.

**Tech Stack:** ESP-IDF 6.0.2, `driver/i2s_std`, SPIFFS, `esp_timer`, `FreeRTOS`, `uart`, `nvs_flash`.

## Global Constraints

- Do not modify the original `机器狗` project in place.
- Use ESP-IDF, not Arduino / PlatformIO.
- Target chip is ESP32-S3-N16R8.
- I2S output pins are fixed: BCLK GPIO14, LRC GPIO15, DIN GPIO16.
- Audio format is 16-bit, Philips I2S, 16 kHz, stereo output with identical L/R samples.
- Playback must use chunked DMA writes and must not rely on long `delay()`.
- Add trailing silence before stopping playback.
- Serial console runs at 115200 baud and must report startup diagnostics and failure reasons.

---

### Task 1: Create the isolated `robotdogsay` project copy

**Files:**
- Create: `robotdogsay/CMakeLists.txt`
- Create: `robotdogsay/main/CMakeLists.txt`
- Create: `robotdogsay/main/idf_component.yml`
- Create: `robotdogsay/main/main.c`
- Create: `robotdogsay/main/app_config.h`
- Create: `robotdogsay/main/audio_player.c`
- Create: `robotdogsay/main/audio_player.h`
- Create: `robotdogsay/main/spiffs_mount.c`
- Create: `robotdogsay/main/spiffs_mount.h`
- Create: `robotdogsay/main/console_cmd.c`
- Create: `robotdogsay/main/console_cmd.h`
- Create: `robotdogsay/main/fixed_clips.h`
- Create: `robotdogsay/spiffs/audio/boot.wav`
- Create: `robotdogsay/spiffs/audio/hello.wav`
- Create: `robotdogsay/spiffs/audio/status.wav`

**Interfaces:**
- Consumes: existing ESP-IDF 6.0.2 installation at `/home/wh/esp/esp-idf`
- Produces: a standalone `robotdogsay` tree with the original project left untouched

- [ ] **Step 1: Write the failing test**

Create a small host-side sanity script at `robotdogsay/tools/check_layout.sh` that fails if `robotdogsay/main/main.c` does not exist:

```bash
#!/usr/bin/env bash
set -euo pipefail
[ -f robotdogsay/main/main.c ]
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash robotdogsay/tools/check_layout.sh`
Expected: fail because the new project tree does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Copy the existing ESP-IDF project into `robotdogsay/`, then replace the old servo/web entry files with the new audio-oriented files above.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash robotdogsay/tools/check_layout.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add robotdogsay
git commit -m "feat: start robotdogsay audio project"
```

### Task 2: Add SPIFFS storage and mount it at boot

**Files:**
- Create: `robotdogsay/partitions.csv`
- Modify: `robotdogsay/CMakeLists.txt`
- Modify: `robotdogsay/main/spiffs_mount.c`
- Modify: `robotdogsay/main/spiffs_mount.h`
- Modify: `robotdogsay/main/main.c`

**Interfaces:**
- Consumes: `bool spiffs_mount_or_abort(void);`
- Produces: `const char *spiffs_root_path(void);`

- [ ] **Step 1: Write the failing test**

Add a host-side check script at `robotdogsay/tools/check_spiffs.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
grep -q '^storage, data, spiffs' robotdogsay/partitions.csv
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash robotdogsay/tools/check_spiffs.sh`
Expected: fail until the custom partition table exists.

- [ ] **Step 3: Write minimal implementation**

Create a custom partition table with one SPIFFS partition for audio clips and wire it into the project CMake so SPIFFS assets are flashed with the app.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash robotdogsay/tools/check_spiffs.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add robotdogsay/CMakeLists.txt robotdogsay/partitions.csv robotdogsay/main/spiffs_mount.c robotdogsay/main/spiffs_mount.h robotdogsay/main/main.c
git commit -m "feat: mount spiffs for fixed voice clips"
```

### Task 3: Implement the I2S playback engine for MAX98357A

**Files:**
- Create: `robotdogsay/main/audio_player.c`
- Create: `robotdogsay/main/audio_player.h`
- Modify: `robotdogsay/main/main.c`

**Interfaces:**
- Consumes: `esp_err_t audio_player_init(void);`
- Consumes: `esp_err_t audio_player_play_wav_file(const char *path, int volume);`
- Consumes: `esp_err_t audio_player_stop(void);`
- Produces: non-blocking chunked playback through `i2s_channel_write`

- [ ] **Step 1: Write the failing test**

Add a small host check at `robotdogsay/tools/check_audio_api.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
grep -q 'audio_player_play_wav_file' robotdogsay/main/audio_player.h
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash robotdogsay/tools/check_audio_api.sh`
Expected: fail until the audio player API exists.

- [ ] **Step 3: Write minimal implementation**

Implement `audio_player_init()` with `i2s_new_channel()`, `i2s_channel_init_std_mode()`, and `i2s_channel_enable()`. Use 16-bit stereo Philips format at 16 kHz, keep L/R equal, write DMA in chunks, and append silence before stop.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash robotdogsay/tools/check_audio_api.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add robotdogsay/main/audio_player.c robotdogsay/main/audio_player.h robotdogsay/main/main.c
git commit -m "feat: add i2s playback engine"
```

### Task 4: Add fixed clips and serial commands

**Files:**
- Create: `robotdogsay/main/console_cmd.c`
- Create: `robotdogsay/main/console_cmd.h`
- Create: `robotdogsay/main/fixed_clips.h`
- Modify: `robotdogsay/main/main.c`

**Interfaces:**
- Consumes: `void console_cmd_task(void *arg);`
- Consumes: `const fixed_clip_t *fixed_clip_by_name(const char *name);`
- Produces: serial commands `h`, `1`, `2`, `3`, `v0`-`v9`

- [ ] **Step 1: Write the failing test**

Add a host check at `robotdogsay/tools/check_console.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
grep -q 'v0-v9' robotdogsay/main/console_cmd.c
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash robotdogsay/tools/check_console.sh`
Expected: fail until the command parser exists.

- [ ] **Step 3: Write minimal implementation**

Create a command loop that prints help, selects one of the fixed SPIFFS clips, and adjusts playback volume without blocking the main task.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash robotdogsay/tools/check_console.sh`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add robotdogsay/main/console_cmd.c robotdogsay/main/console_cmd.h robotdogsay/main/fixed_clips.h robotdogsay/main/main.c
git commit -m "feat: add fixed clip console control"
```

### Task 5: Verify build, flash, and startup behavior

**Files:**
- Modify: `robotdogsay/README.md`

**Interfaces:**
- Consumes: `idf.py build`, `idf.py flash`, `idf.py monitor`
- Produces: documented build steps and self-test steps

- [ ] **Step 1: Build the project**

Run:

```bash
cd /home/wh/esp/robotdogsay
. /home/wh/esp/esp-idf/export.sh
idf.py build
```

Expected: zero compile errors.

- [ ] **Step 2: Flash and monitor**

Run:

```bash
idf.py flash monitor
```

Expected: boot log prints chip model, flash size, PSRAM size, SPIFFS mount result, and I2S init result.

- [ ] **Step 3: Confirm playback**

Expected: boot clip plays cleanly, then serial commands `1`, `2`, `3`, and `v0`-`v9` work without resetting the board.

- [ ] **Step 4: Commit**

```bash
git add robotdogsay/README.md
git commit -m "docs: add robotdogsay usage notes"
```

## Self-Review

Coverage check:

- Project copy and isolation: Task 1
- SPIFFS partition and mount: Task 2
- I2S init and playback rules: Task 3
- Fixed file-backed clips and commands: Task 4
- Build and boot verification: Task 5

Placeholder scan:

- No TBD/TODO markers remain.
- File paths are explicit.
- Public interfaces are named before later tasks use them.

Scope check:

- This plan is scoped to fixed-file playback only.
- Online TTS and streaming are intentionally out of scope for this pass.
