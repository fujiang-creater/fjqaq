#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../main/capture_gate.h"

int main(void)
{
    mic_capture_gate_t gate;
    mic_capture_gate_init(&gate);

    assert(mic_capture_gate_update(&gate, true) == MIC_CAPTURE_GATE_PRESS);
    mic_capture_gate_reject_press(&gate);

    /* A held button must not cause another press event after rejection. */
    assert(mic_capture_gate_update(&gate, true) == MIC_CAPTURE_GATE_NONE);
    assert(mic_capture_gate_update(&gate, true) == MIC_CAPTURE_GATE_NONE);

    /* Releasing clears the retry gate and permits the next press. */
    assert(mic_capture_gate_update(&gate, false) == MIC_CAPTURE_GATE_NONE);
    assert(mic_capture_gate_update(&gate, true) == MIC_CAPTURE_GATE_PRESS);

    mic_capture_gate_transport_lost(&gate);
    assert(mic_capture_gate_update(&gate, true) == MIC_CAPTURE_GATE_NONE);
    mic_capture_gate_force_release(&gate);
    assert(mic_capture_gate_update(&gate, false) == MIC_CAPTURE_GATE_NONE);

    puts("capture gate test passed");
    return 0;
}
