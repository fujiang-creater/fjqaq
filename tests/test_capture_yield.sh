#!/usr/bin/env bash
set -euo pipefail

source_file="$(dirname "$0")/../main/mic_board.c"

if ! rg -q 'vTaskDelay\(1\);' "$source_file"; then
    echo "capture task must yield for at least one FreeRTOS tick while idle" >&2
    exit 1
fi

if rg -q 'vTaskDelay\(pdMS_TO_TICKS\(5\)\)' "$source_file"; then
    echo "capture task still uses a sub-tick idle delay" >&2
    exit 1
fi

echo "capture yield test passed"
