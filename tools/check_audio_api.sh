#!/usr/bin/env bash
set -euo pipefail
grep -q 'esp_err_t audio_player_init(void);' robotdogsay/main/audio_player.h
