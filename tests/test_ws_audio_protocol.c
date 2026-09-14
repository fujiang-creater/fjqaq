#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../main/ws_audio_protocol.h"

int main(void)
{
    const uint8_t opus[] = {0x11, 0x22, 0x33};
    uint8_t packet[7] = {0};

    size_t written = mic_ws_wrap_opus_v3(opus, sizeof(opus), packet, sizeof(packet));
    assert(written == 7);
    assert(packet[0] == 0x00);
    assert(packet[1] == 0x00);
    assert(packet[2] == 0x00);
    assert(packet[3] == 0x03);
    assert(packet[4] == 0x11);
    assert(packet[5] == 0x22);
    assert(packet[6] == 0x33);

    assert(mic_ws_wrap_opus_v3(opus, sizeof(opus), packet, 6) == 0);
    assert(mic_ws_wrap_opus_v3(NULL, sizeof(opus), packet, sizeof(packet)) == 0);
    assert(mic_ws_wrap_opus_v3(opus, 0, packet, 4) == 4);

    puts("WebSocket Version 3 audio protocol test passed");
    return 0;
}
