#include "mm2astrisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_active_session(mm2a_context_t *ctx);

static void on_call_state_changed(MMCall *call,
                                  gint old_state,
                                  gint new_state,
                                  guint reason,
                                  gpointer user_data) {
    mm2a_context_t *ctx = (mm2a_context_t *)user_data;
    const gchar *path = mm_call_get_path(call);

    g_message("Call %s: State changed from %d to %d (reason: %u)",
              path, old_state, new_state, reason);

    if (new_state == MM_CALL_STATE_TERMINATED) {
        g_message("Call %s terminated.", path);
        if (ctx->session.active && ctx->session.call == call) {
            cleanup_active_session(ctx);
        }
    }
}

static void on_call_accept_ready(GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data) {
    (void)user_data;
    MMCall *call = MM_CALL(source_object);
    GError *error = NULL;
    if (!mm_call_accept_finish(call, res, &error)) {
        g_warning("Failed to accept call %s: %s", mm_call_get_path(call), error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        g_message("Call %s successfully accepted and active.", mm_call_get_path(call));
    }
}

static void on_call_hangup_ready(GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data) {
    (void)user_data;
    MMCall *call = MM_CALL(source_object);
    GError *error = NULL;
    if (!mm_call_hangup_finish(call, res, &error)) {
        g_warning("Failed to hangup call %s: %s", mm_call_get_path(call), error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        g_message("Call %s successfully hung up.", mm_call_get_path(call));
    }
}

void mm2a_call_accept(mm2a_context_t *ctx, MMCall *call) {
    g_message("Accepting call %s...", mm_call_get_path(call));
    mm_call_accept(call, NULL, on_call_accept_ready, ctx);
}

void mm2a_call_hangup(mm2a_context_t *ctx, MMCall *call) {
    g_message("Hanging up call %s...", mm_call_get_path(call));
    mm_call_hangup(call, NULL, on_call_hangup_ready, ctx);
}

static void cleanup_active_session(mm2a_context_t *ctx) {
    mm2a_session_t *sess = &ctx->session;
    if (!sess->active) return;

    g_message("Cleaning up active call session...");
    sess->active = FALSE;

    mm2a_audio_stop(ctx);

    if (sess->socket_fd >= 0) {
        audiosocket_close(sess->socket_fd);
        sess->socket_fd = -1;
    }

    if (ctx->voice_modem && sess->call_path) {
        mm_modem_voice_delete_call(ctx->voice_modem, sess->call_path, NULL, NULL, NULL);
    }

    if (sess->call) {
        g_object_unref(sess->call);
        sess->call = NULL;
    }

    g_free(sess->call_path);
    sess->call_path = NULL;

    g_free(sess->caller_number);
    sess->caller_number = NULL;

    g_message("Session cleanup complete. Waiting for new calls.");
}

static void handle_incoming_call(mm2a_context_t *ctx, MMCall *call) {
    const gchar *path = mm_call_get_path(call);
    const gchar *number = mm_call_get_number(call);
    MMCallDirection dir = mm_call_get_direction(call);
    MMCallState state = mm_call_get_state(call);

    g_message("New Call detected: path=%s, number='%s', direction=%d, state=%d",
              path, number ? number : "(unknown)", dir, state);

    if (dir != MM_CALL_DIRECTION_INCOMING) {
        g_message("Ignoring outgoing call %s", path);
        return;
    }

    if (ctx->session.active) {
        g_message("Busy: Another call is currently active. Rejecting %s", path);
        mm2a_call_hangup(ctx, call);
        return;
    }

    if (!mm2a_config_is_whitelisted(&ctx->config, number)) {
        g_warning("SECURITY: Caller '%s' is not in whitelist! Rejecting call.", number ? number : "unknown");
        mm2a_call_hangup(ctx, call);
        return;
    }

    g_message("Authorized caller '%s'. Connecting to server (%s:%d)...",
              number ? number : "unknown", ctx->config.server_host, ctx->config.server_port);

    int sock_fd = audiosocket_connect(ctx->config.server_host,
                                      ctx->config.server_port,
                                      ctx->config.uuid_str);
    if (sock_fd < 0) {
        g_warning("Could not establish AudioSocket connection to server! Rejecting call.");
        mm2a_call_hangup(ctx, call);
        return;
    }

    ctx->session.active = TRUE;
    ctx->session.call = g_object_ref(call);
    ctx->session.call_path = g_strdup(path);
    ctx->session.caller_number = g_strdup(number);
    ctx->session.socket_fd = sock_fd;
    ctx->session.config = &ctx->config;

    g_signal_connect(call, "state-changed", G_CALLBACK(on_call_state_changed), ctx);

    if (mm2a_audio_start(ctx) < 0) {
        g_warning("Failed to start audio subsystem! Aborting call.");
        cleanup_active_session(ctx);
        mm2a_call_hangup(ctx, call);
        return;
    }

    mm2a_call_accept(ctx, call);
}

static void on_call_added(MMModemVoice *voice, const gchar *path, gpointer user_data) {
    mm2a_context_t *ctx = (mm2a_context_t *)user_data;
    g_message("ModemManager signal: call-added -> %s", path);

    GError *error = NULL;
    GList *calls = mm_modem_voice_list_calls_sync(voice, NULL, &error);
    if (error) {
        g_warning("Failed to list calls: %s", error->message);
        g_clear_error(&error);
        return;
    }

    for (GList *l = calls; l != NULL; l = l->next) {
        MMCall *call = MM_CALL(l->data);
        if (g_strcmp0(mm_call_get_path(call), path) == 0) {
            handle_incoming_call(ctx, call);
            break;
        }
    }

    g_list_free_full(calls, g_object_unref);
}

static void on_call_deleted(MMModemVoice *voice, const gchar *path, gpointer user_data) {
    (void)voice;
    mm2a_context_t *ctx = (mm2a_context_t *)user_data;
    g_message("ModemManager signal: call-deleted -> %s", path);

    if (ctx->session.active && g_strcmp0(ctx->session.call_path, path) == 0) {
        cleanup_active_session(ctx);
    }
}

void mm2a_modem_scan(GDBusConnection *connection) {
    GError *error = NULL;
    MMManager *mgr = mm_manager_new_sync(connection, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, NULL, &error);
    if (!mgr) {
        g_printerr("Failed to connect to ModemManager: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    GList *objects = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(mgr));
    g_print("\n=== ModemManager Scan Results ===\n");
    if (!objects) {
        g_print("No modems found on this system.\n");
        g_object_unref(mgr);
        return;
    }

    for (GList *l = objects; l != NULL; l = l->next) {
        MMObject *obj = MM_OBJECT(l->data);
        MMModem *modem = mm_object_get_modem(obj);
        MMModemVoice *voice = mm_object_get_modem_voice(obj);

        const gchar *path = mm_object_get_path(obj);
        const gchar *mfr = modem ? mm_modem_get_manufacturer(modem) : "Unknown";
        const gchar *model = modem ? mm_modem_get_model(modem) : "Unknown";
        gboolean has_voice = (voice != NULL);

        g_print("Modem: %s\n", path);
        g_print("  Manufacturer: %s\n", mfr ? mfr : "Unknown");
        g_print("  Model:        %s\n", model ? model : "Unknown");
        g_print("  Voice Support: %s\n\n", has_voice ? "YES (Compatible with mm2astrisk)" : "NO");

        if (modem) g_object_unref(modem);
        if (voice) g_object_unref(voice);
    }

    g_list_free_full(objects, g_object_unref);
    g_object_unref(mgr);
}

int mm2a_modem_init(mm2a_context_t *ctx) {
    GError *error = NULL;
    ctx->mm_manager = mm_manager_new_sync(ctx->dbus_conn,
                                          G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                          NULL,
                                          &error);
    if (!ctx->mm_manager) {
        g_warning("Could not connect to ModemManager: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        return -1;
    }

    GList *objects = g_dbus_object_manager_get_objects(G_DBUS_OBJECT_MANAGER(ctx->mm_manager));
    if (!objects) {
        g_warning("No modems currently available in ModemManager.");
        return -1;
    }

    MMModemVoice *selected_voice = NULL;

    for (GList *l = objects; l != NULL; l = l->next) {
        MMObject *obj = MM_OBJECT(l->data);
        const gchar *path = mm_object_get_path(obj);
        MMModemVoice *voice = mm_object_get_modem_voice(obj);

        if (voice) {
            if (g_strcmp0(ctx->config.modem_path, "auto") == 0 ||
                g_strcmp0(ctx->config.modem_path, path) == 0) {
                selected_voice = voice;
                g_message("Selected voice-capable modem: %s", path);
                break;
            }
            g_object_unref(voice);
        }
    }

    g_list_free_full(objects, g_object_unref);

    if (!selected_voice) {
        g_warning("No suitable voice-capable modem found (requested: %s)", ctx->config.modem_path);
        return -1;
    }

    ctx->voice_modem = selected_voice;
    g_signal_connect(selected_voice, "call-added", G_CALLBACK(on_call_added), ctx);
    g_signal_connect(selected_voice, "call-deleted", G_CALLBACK(on_call_deleted), ctx);

    g_message("ModemManager voice interface successfully initialized.");
    return 0;
}

void mm2a_modem_cleanup(mm2a_context_t *ctx) {
    cleanup_active_session(ctx);

    if (ctx->voice_modem) {
        g_object_unref(ctx->voice_modem);
        ctx->voice_modem = NULL;
    }
    if (ctx->mm_manager) {
        g_object_unref(ctx->mm_manager);
        ctx->mm_manager = NULL;
    }
}
