#include "capture_gate.h"

#include <stddef.h>

void mic_capture_gate_init(mic_capture_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }

    gate->button_pressed = false;
    gate->wait_for_release = false;
}

mic_capture_gate_event_t mic_capture_gate_update(mic_capture_gate_t *gate,
                                                 bool stable_pressed)
{
    if (gate == NULL) {
        return MIC_CAPTURE_GATE_NONE;
    }

    if (gate->wait_for_release) {
        if (!stable_pressed) {
            gate->button_pressed = false;
            gate->wait_for_release = false;
        }
        return MIC_CAPTURE_GATE_NONE;
    }

    if (stable_pressed && !gate->button_pressed) {
        gate->button_pressed = true;
        return MIC_CAPTURE_GATE_PRESS;
    }

    if (!stable_pressed && gate->button_pressed) {
        gate->button_pressed = false;
        return MIC_CAPTURE_GATE_RELEASE;
    }

    return MIC_CAPTURE_GATE_NONE;
}

void mic_capture_gate_reject_press(mic_capture_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }

    gate->button_pressed = true;
    gate->wait_for_release = true;
}

void mic_capture_gate_force_release(mic_capture_gate_t *gate)
{
    if (gate == NULL) {
        return;
    }

    gate->button_pressed = true;
    gate->wait_for_release = true;
}

void mic_capture_gate_transport_lost(mic_capture_gate_t *gate)
{
    mic_capture_gate_force_release(gate);
}
