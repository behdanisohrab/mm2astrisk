#include "mm2astrisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

static int parse_hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int uuid_from_string(const char *str, uint8_t bin[16]) {
    if (!str) return -1;
    int b = 0;
    size_t len = strlen(str);
    for (size_t i = 0; i < len && b < 16; i++) {
        if (str[i] == '-') continue;
        if (i + 1 >= len) return -1;
        int h1 = parse_hex_char(str[i]);
        int h2 = parse_hex_char(str[++i]);
        if (h1 < 0 || h2 < 0) return -1;
        bin[b++] = (uint8_t)((h1 << 4) | h2);
    }
    return (b == 16) ? 0 : -1;
}

static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const uint8_t *p = (const uint8_t *)buf;
    while (total < count) {
        ssize_t n = write(fd, p + total, count - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static ssize_t read_all(int fd, void *buf, size_t count) {
    size_t total = 0;
    uint8_t *p = (uint8_t *)buf;
    while (total < count) {
        ssize_t n = read(fd, p + total, count - total);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            return (n == 0) ? (ssize_t)total : -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

int audiosocket_connect(const char *host, int port, const char *uuid_str) {
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        g_warning("AudioSocket: getaddrinfo failed for %s:%d: %s", host, port, gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd == -1) {
        g_warning("AudioSocket: Could not connect to %s:%d (%s)", host, port, strerror(errno));
        return -1;
    }

    uint8_t uuid_bin[16];
    if (uuid_from_string(uuid_str, uuid_bin) != 0) {
        memset(uuid_bin, 0, sizeof(uuid_bin));
    }

    uint8_t id_hdr[3];
    id_hdr[0] = AUDIOSOCKET_TYPE_ID;
    uint16_t id_len = htons(16);
    memcpy(&id_hdr[1], &id_len, 2);

    if (write_all(fd, id_hdr, 3) != 3 || write_all(fd, uuid_bin, 16) != 16) {
        g_warning("AudioSocket: Failed to send ID handshake");
        close(fd);
        return -1;
    }

    g_message("AudioSocket: Connected to %s:%d with UUID %s", host, port, uuid_str ? uuid_str : "null");
    return fd;
}

int audiosocket_send_audio(int fd, const uint8_t *pcm_data, size_t len) {
    if (fd < 0 || !pcm_data || len == 0 || len > 65535)
        return -1;

    uint8_t hdr[3];
    hdr[0] = AUDIOSOCKET_TYPE_AUDIO;
    uint16_t net_len = htons((uint16_t)len);
    memcpy(&hdr[1], &net_len, 2);

    if (write_all(fd, hdr, 3) != 3)
        return -1;
    if (write_all(fd, pcm_data, len) != (ssize_t)len)
        return -1;

    return 0;
}

int audiosocket_recv_frame(int fd, uint8_t *type, uint8_t *buf, size_t max_len) {
    if (fd < 0 || !type) return -1;

    uint8_t hdr[3];
    ssize_t n = read_all(fd, hdr, 3);
    if (n != 3) {
        return -1;
    }

    *type = hdr[0];
    uint16_t net_len;
    memcpy(&net_len, &hdr[1], 2);
    uint16_t payload_len = ntohs(net_len);

    if (payload_len == 0) {
        return 0;
    }

    if (payload_len > max_len) {
        g_warning("AudioSocket: Payload too large (%u > %zu)", payload_len, max_len);
        uint8_t dummy[256];
        size_t remaining = payload_len;
        while (remaining > 0) {
            size_t to_read = remaining > sizeof(dummy) ? sizeof(dummy) : remaining;
            if (read_all(fd, dummy, to_read) != (ssize_t)to_read) return -1;
            remaining -= to_read;
        }
        return -1;
    }

    if (read_all(fd, buf, payload_len) != (ssize_t)payload_len) {
        return -1;
    }

    return (int)payload_len;
}

void audiosocket_send_hangup(int fd) {
    if (fd < 0) return;
    uint8_t hdr[3] = { AUDIOSOCKET_TYPE_HANGUP, 0x00, 0x00 };
    write_all(fd, hdr, 3);
}

void audiosocket_close(int fd) {
    if (fd >= 0) {
        audiosocket_send_hangup(fd);
        close(fd);
    }
}
