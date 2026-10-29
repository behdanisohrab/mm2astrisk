#include "mm2astrisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

static int init_pcm(snd_pcm_t **handle_out, const char *dev_name, snd_pcm_stream_t stream, unsigned int rate) {
    snd_pcm_t *handle;
    int err;

    err = snd_pcm_open(&handle, dev_name, stream, 0);
    if (err < 0) {
        g_warning("ALSA: Cannot open %s for %s: %s", dev_name,
                  stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback",
                  snd_strerror(err));
        return -1;
    }

    err = snd_pcm_set_params(handle,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             MM2A_DEFAULT_CHANNELS,
                             rate,
                             1,
                             50000);
    if (err < 0) {
        g_warning("ALSA: Cannot set parameters for %s: %s", dev_name, snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }

    *handle_out = handle;
    return 0;
}

static void *capture_worker(void *arg) {
    mm2a_context_t *ctx = (mm2a_context_t *)arg;
    mm2a_session_t *sess = &ctx->session;
    unsigned int rate = ctx->config.sample_rate;
    snd_pcm_uframes_t period_frames = (rate * MM2A_FRAME_MS) / 1000;
    size_t period_bytes = period_frames * sizeof(int16_t);
    int16_t *buf = malloc(period_bytes);

    if (!buf) {
        g_warning("ALSA Capture: malloc failed");
        return NULL;
    }

    g_message("ALSA: Capture worker started (%lu frames/period, %zu bytes)", period_frames, period_bytes);

    while (!sess->stop_audio && sess->active) {
        snd_pcm_sframes_t frames = snd_pcm_readi(sess->pcm_capture, buf, period_frames);
        if (frames < 0) {
            frames = snd_pcm_recover(sess->pcm_capture, (int)frames, 0);
            if (frames < 0) {
                g_warning("ALSA Capture error: %s", snd_strerror((int)frames));
                usleep(10000);
                continue;
            }
        }

        if (frames > 0 && sess->socket_fd >= 0) {
            size_t bytes_to_send = frames * sizeof(int16_t);
            if (audiosocket_send_audio(sess->socket_fd, (const uint8_t *)buf, bytes_to_send) < 0) {
                g_warning("AudioSocket: Send failed, stopping audio");
                break;
            }
        }
    }

    free(buf);
    g_message("ALSA: Capture worker finished");
    return NULL;
}

static void *playback_worker(void *arg) {
    mm2a_context_t *ctx = (mm2a_context_t *)arg;
    mm2a_session_t *sess = &ctx->session;
    uint8_t rx_buf[4096];

    g_message("ALSA: Playback worker started");

    while (!sess->stop_audio && sess->active) {
        uint8_t type = 0;
        int n = audiosocket_recv_frame(sess->socket_fd, &type, rx_buf, sizeof(rx_buf));
        if (n < 0) {
            g_warning("AudioSocket: Connection closed or read error");
            break;
        }

        if (type == AUDIOSOCKET_TYPE_HANGUP) {
            g_message("AudioSocket: Received HANGUP signal");
            if (sess->call) {
                mm2a_call_hangup(ctx, sess->call);
            }
            break;
        } else if (type == AUDIOSOCKET_TYPE_AUDIO && n > 0) {
            snd_pcm_uframes_t frames = n / sizeof(int16_t);
            snd_pcm_sframes_t written = snd_pcm_writei(sess->pcm_playback, rx_buf, frames);
            if (written < 0) {
                written = snd_pcm_recover(sess->pcm_playback, (int)written, 0);
                if (written < 0) {
                    g_warning("ALSA Playback underrun recovery failed: %s", snd_strerror((int)written));
                }
            }
        }
    }

    g_message("ALSA: Playback worker finished");
    return NULL;
}

int mm2a_audio_start(mm2a_context_t *ctx) {
    mm2a_session_t *sess = &ctx->session;

    if (init_pcm(&sess->pcm_capture, ctx->config.capture_device, SND_PCM_STREAM_CAPTURE, ctx->config.sample_rate) < 0) {
        return -1;
    }
    if (init_pcm(&sess->pcm_playback, ctx->config.playback_device, SND_PCM_STREAM_PLAYBACK, ctx->config.sample_rate) < 0) {
        snd_pcm_close(sess->pcm_capture);
        sess->pcm_capture = NULL;
        return -1;
    }

    sess->stop_audio = FALSE;
    pthread_create(&sess->capture_thread, NULL, capture_worker, ctx);
    pthread_create(&sess->playback_thread, NULL, playback_worker, ctx);

    return 0;
}

void mm2a_audio_stop(mm2a_context_t *ctx) {
    mm2a_session_t *sess = &ctx->session;
    sess->stop_audio = TRUE;

    if (sess->pcm_capture) {
        snd_pcm_abort(sess->pcm_capture);
    }
    if (sess->pcm_playback) {
        snd_pcm_abort(sess->pcm_playback);
    }

    if (sess->capture_thread) {
        pthread_join(sess->capture_thread, NULL);
        sess->capture_thread = 0;
    }
    if (sess->playback_thread) {
        pthread_join(sess->playback_thread, NULL);
        sess->playback_thread = 0;
    }

    if (sess->pcm_capture) {
        snd_pcm_close(sess->pcm_capture);
        sess->pcm_capture = NULL;
    }
    if (sess->pcm_playback) {
        snd_pcm_close(sess->pcm_playback);
        sess->pcm_playback = NULL;
    }
}
