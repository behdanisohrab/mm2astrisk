#include "mm2astrisk.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    g_print("[TEST] Running AudioSocket framing unit tests...\n");

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    uint8_t pcm_data[320];
    for (int i = 0; i < 320; i++) pcm_data[i] = (uint8_t)(i & 0xFF);

    assert(audiosocket_send_audio(sv[0], pcm_data, sizeof(pcm_data)) == 0);

    uint8_t type = 0;
    uint8_t rx_buf[512];
    int n = audiosocket_recv_frame(sv[1], &type, rx_buf, sizeof(rx_buf));

    assert(n == 320);
    assert(type == AUDIOSOCKET_TYPE_AUDIO);
    assert(memcmp(pcm_data, rx_buf, 320) == 0);

    audiosocket_send_hangup(sv[0]);
    type = 0;
    n = audiosocket_recv_frame(sv[1], &type, rx_buf, sizeof(rx_buf));
    assert(n == 0);
    assert(type == AUDIOSOCKET_TYPE_HANGUP);

    close(sv[0]);
    close(sv[1]);

    g_print("[PASS] AudioSocket framing tests passed successfully.\n");
    return 0;
}
