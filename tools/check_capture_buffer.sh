#!/usr/bin/env bash
set -euo pipefail

grep -q 'static void discard_i2s_input(int32_t \*input, size_t input_bytes)' main/mic_board.c
! grep -q 'int32_t input\[APP_I2S_SLOT_COUNT\]' main/mic_board.c
