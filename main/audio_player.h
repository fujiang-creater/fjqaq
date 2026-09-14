#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "fixed_clips.h"

esp_err_t audio_player_init(void);
esp_err_t audio_player_play_clip(fixed_clip_id_t clip_id);
esp_err_t audio_player_play_demo_sequence(void);
esp_err_t audio_player_set_volume(uint8_t volume);
uint8_t audio_player_get_volume(void);
bool audio_player_is_busy(void);
esp_err_t audio_player_stop(void);
