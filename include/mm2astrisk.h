#ifndef MM2ASTRISK_H
#define MM2ASTRISK_H

#include <glib.h>
#include <gio/gio.h>
#include <libmm-glib.h>
#include <stdint.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define MM2A_VERSION "1.0.0"
#define MM2A_MAX_WHITELIST 64
#define MM2A_DEFAULT_RATE 8000
#define MM2A_DEFAULT_CHANNELS 1
#define MM2A_DEFAULT_PORT 9092
#define MM2A_FRAME_MS 20

#define AUDIOSOCKET_TYPE_HANGUP 0x00
#define AUDIOSOCKET_TYPE_ID     0x01
#define AUDIOSOCKET_TYPE_AUDIO  0x10
#define AUDIOSOCKET_TYPE_ERROR  0x02

typedef struct {
    char *modem_path;
    char *whitelist[MM2A_MAX_WHITELIST];
    int whitelist_count;
    gboolean allow_all_callers;
    char *capture_device;
    char *playback_device;
    unsigned int sample_rate;
    char *server_host;
    int server_port;
    char *uuid_str;
    gboolean debug;
} mm2a_config_t;

typedef struct {
    gboolean active;
    MMCall *call;
    char *call_path;
    char *caller_number;
    int socket_fd;
    snd_pcm_t *pcm_capture;
    snd_pcm_t *pcm_playback;
    pthread_t capture_thread;
    pthread_t playback_thread;
    volatile gboolean stop_audio;
    mm2a_config_t *config;
} mm2a_session_t;

typedef struct {
    mm2a_config_t config;
    GMainLoop *loop;
    GDBusConnection *dbus_conn;
    MMManager *mm_manager;
    MMModemVoice *voice_modem;
    mm2a_session_t session;
} mm2a_context_t;

int mm2a_config_load(const char *path, mm2a_config_t *cfg);
void mm2a_config_free(mm2a_config_t *cfg);
gboolean mm2a_config_is_whitelisted(const mm2a_config_t *cfg, const char *number);

int mm2a_modem_init(mm2a_context_t *ctx);
void mm2a_modem_cleanup(mm2a_context_t *ctx);
void mm2a_modem_scan(GDBusConnection *connection);
void mm2a_call_accept(mm2a_context_t *ctx, MMCall *call);
void mm2a_call_hangup(mm2a_context_t *ctx, MMCall *call);

int audiosocket_connect(const char *host, int port, const char *uuid_str);
int audiosocket_send_audio(int fd, const uint8_t *pcm_data, size_t len);
int audiosocket_recv_frame(int fd, uint8_t *type, uint8_t *buf, size_t max_len);
void audiosocket_send_hangup(int fd);
void audiosocket_close(int fd);

int mm2a_audio_start(mm2a_context_t *ctx);
void mm2a_audio_stop(mm2a_context_t *ctx);

#endif
