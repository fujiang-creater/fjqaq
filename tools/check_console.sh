#!/usr/bin/env bash
set -euo pipefail
grep -q 'esp_err_t console_cmd_start(void);' robotdogsay/main/console_cmd.h
