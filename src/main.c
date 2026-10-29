#include "mm2astrisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib-unix.h>

static gboolean on_unix_signal(gpointer user_data) {
    mm2a_context_t *ctx = (mm2a_context_t *)user_data;
    g_message("Termination signal received. Exiting gracefully...");
    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }
    return G_SOURCE_REMOVE;
}

static void print_usage(const char *prog) {
    g_print("Usage: %s [options]\n\n"
            "Options:\n"
            "  -c, --config <file>   Path to configuration file (default: config.ini)\n"
            "  -s, --scan            Scan and display available modems with voice support and exit\n"
            "  -d, --debug           Enable verbose debug logging\n"
            "  -v, --version         Show version information\n"
            "  -h, --help            Show this help message\n\n", prog);
}

int main(int argc, char *argv[]) {
    const char *config_path = "config.ini";
    gboolean do_scan = FALSE;
    gboolean debug_flag = FALSE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                g_printerr("Error: Missing argument for %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scan") == 0) {
            do_scan = TRUE;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_flag = TRUE;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            g_print("mm2astrisk version %s\n", MM2A_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            g_printerr("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!bus) {
        g_printerr("Fatal: Failed to connect to System D-Bus: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }

    if (do_scan) {
        mm2a_modem_scan(bus);
        g_object_unref(bus);
        return 0;
    }

    mm2a_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.dbus_conn = bus;

    g_message("Starting mm2astrisk v%s...", MM2A_VERSION);
    if (mm2a_config_load(config_path, &ctx.config) != 0) {
        g_printerr("Error loading configuration from %s\n", config_path);
        g_object_unref(bus);
        return 1;
    }

    if (debug_flag) {
        ctx.config.debug = TRUE;
    }

    g_message("Config loaded: server=%s:%d, rate=%u, whitelist_entries=%d, allow_all=%s",
              ctx.config.server_host, ctx.config.server_port, ctx.config.sample_rate,
              ctx.config.whitelist_count, ctx.config.allow_all_callers ? "yes" : "no");

    if (mm2a_modem_init(&ctx) < 0) {
        g_printerr("Failed to initialize ModemManager voice interface.\n"
                   "Make sure ModemManager is running and a voice-capable modem is detected.\n"
                   "You can run '%s --scan' to check available modems.\n", argv[0]);
        mm2a_config_free(&ctx.config);
        g_object_unref(bus);
        return 1;
    }

    ctx.loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGINT, on_unix_signal, &ctx);
    g_unix_signal_add(SIGTERM, on_unix_signal, &ctx);

    g_message("mm2astrisk is running. Waiting for incoming cellular calls...");
    g_main_loop_run(ctx.loop);

    g_message("Shutting down...");
    mm2a_modem_cleanup(&ctx);
    mm2a_config_free(&ctx.config);
    g_main_loop_unref(ctx.loop);
    g_object_unref(bus);

    g_message("Shutdown complete.");
    return 0;
}
