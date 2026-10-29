#include "mm2astrisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static void normalize_phone_number(const char *in, char *out, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < max_len - 1; i++) {
        if (isdigit((unsigned char)in[i]) || in[i] == '+') {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static void parse_whitelist(mm2a_config_t *cfg, const char *val) {
    char *copy = g_strdup(val);
    char *token = strtok(copy, ",");
    while (token && cfg->whitelist_count < MM2A_MAX_WHITELIST) {
        char *trimmed = trim_whitespace(token);
        if (strlen(trimmed) > 0) {
            cfg->whitelist[cfg->whitelist_count++] = g_strdup(trimmed);
        }
        token = strtok(NULL, ",");
    }
    g_free(copy);
}

int mm2a_config_load(const char *path, mm2a_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->modem_path = g_strdup("auto");
    cfg->capture_device = g_strdup("default");
    cfg->playback_device = g_strdup("default");
    cfg->sample_rate = MM2A_DEFAULT_RATE;
    cfg->server_host = g_strdup("127.0.0.1");
    cfg->server_port = MM2A_DEFAULT_PORT;
    cfg->uuid_str = g_strdup("12345678-1234-5678-1234-567812345678");
    cfg->allow_all_callers = FALSE;
    cfg->debug = FALSE;

    FILE *f = fopen(path, "r");
    if (!f) {
        g_warning("Could not open config file %s, using defaults.", path);
        return 0;
    }

    char line[512];
    char current_section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        char *p = trim_whitespace(line);
        if (*p == '\0' || *p == '#' || *p == ';')
            continue;

        if (*p == '[' && strchr(p, ']')) {
            char *end = strchr(p, ']');
            *end = '\0';
            g_strlcpy(current_section, p + 1, sizeof(current_section));
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_whitespace(p);
        char *val = trim_whitespace(eq + 1);

        if (g_ascii_strcasecmp(key, "modem_path") == 0) {
            g_free(cfg->modem_path);
            cfg->modem_path = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "whitelist") == 0) {
            parse_whitelist(cfg, val);
        } else if (g_ascii_strcasecmp(key, "allow_all") == 0) {
            cfg->allow_all_callers = (g_ascii_strcasecmp(val, "true") == 0 ||
                                      g_ascii_strcasecmp(val, "1") == 0 ||
                                      g_ascii_strcasecmp(val, "yes") == 0);
        } else if (g_ascii_strcasecmp(key, "capture_device") == 0) {
            g_free(cfg->capture_device);
            cfg->capture_device = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "playback_device") == 0) {
            g_free(cfg->playback_device);
            cfg->playback_device = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "sample_rate") == 0) {
            int rate = atoi(val);
            if (rate == 8000 || rate == 16000) {
                cfg->sample_rate = rate;
            } else {
                g_warning("Unsupported sample rate %d, defaulting to 8000", rate);
            }
        } else if (g_ascii_strcasecmp(key, "host") == 0 || g_ascii_strcasecmp(key, "server_host") == 0) {
            g_free(cfg->server_host);
            cfg->server_host = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "port") == 0 || g_ascii_strcasecmp(key, "server_port") == 0) {
            cfg->server_port = atoi(val);
        } else if (g_ascii_strcasecmp(key, "uuid") == 0) {
            g_free(cfg->uuid_str);
            cfg->uuid_str = g_strdup(val);
        } else if (g_ascii_strcasecmp(key, "debug") == 0) {
            cfg->debug = (g_ascii_strcasecmp(val, "true") == 0 ||
                          g_ascii_strcasecmp(val, "1") == 0 ||
                          g_ascii_strcasecmp(val, "yes") == 0);
        }
    }

    fclose(f);
    return 0;
}

void mm2a_config_free(mm2a_config_t *cfg) {
    g_free(cfg->modem_path);
    g_free(cfg->capture_device);
    g_free(cfg->playback_device);
    g_free(cfg->server_host);
    g_free(cfg->uuid_str);
    for (int i = 0; i < cfg->whitelist_count; i++) {
        g_free(cfg->whitelist[i]);
    }
    cfg->whitelist_count = 0;
}

gboolean mm2a_config_is_whitelisted(const mm2a_config_t *cfg, const char *number) {
    if (cfg->allow_all_callers)
        return TRUE;
    if (!number || strlen(number) == 0)
        return FALSE;
    if (cfg->whitelist_count == 0) {
        g_message("Whitelist is empty and allow_all is false: rejecting caller %s", number);
        return FALSE;
    }

    char norm_caller[64];
    normalize_phone_number(number, norm_caller, sizeof(norm_caller));

    for (int i = 0; i < cfg->whitelist_count; i++) {
        char norm_entry[64];
        normalize_phone_number(cfg->whitelist[i], norm_entry, sizeof(norm_entry));

        if (strcmp(norm_caller, norm_entry) == 0)
            return TRUE;

        size_t l_caller = strlen(norm_caller);
        size_t l_entry = strlen(norm_entry);

        if (l_caller >= 9 && l_entry >= 9) {
            const char *s1 = norm_caller + (l_caller - 9);
            const char *s2 = norm_entry + (l_entry - 9);
            if (strcmp(s1, s2) == 0)
                return TRUE;
        }
    }

    return FALSE;
}
