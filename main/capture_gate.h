#pragma once

#include <stdbool.h>

typedef enum {
    MIC_CAPTURE_GATE_NONE = 0,
    MIC_CAPTURE_GATE_PRESS,
    MIC_CAPTURE_GATE_RELEASE,
} mic_capture_gate_event_t;

typedef struct {
    bool button_pressed;
    bool wait_for_release;
} mic_capture_gate_t;

void mic_capture_gate_init(mic_capture_gate_t *gate);
mic_capture_gate_event_t mic_capture_gate_update(mic_capture_gate_t *gate,
                                                 bool stable_pressed);
void mic_capture_gate_reject_press(mic_capture_gate_t *gate);
void mic_capture_gate_force_release(mic_capture_gate_t *gate);
void mic_capture_gate_transport_lost(mic_capture_gate_t *gate);
