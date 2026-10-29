#include "mm2astrisk.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    g_print("[TEST] Running config & whitelist unit tests...\n");

    mm2a_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.allow_all_callers = FALSE;
    cfg.whitelist_count = 0;
    assert(!mm2a_config_is_whitelisted(&cfg, "+989121112233"));

    cfg.whitelist[0] = g_strdup("+989121112233");
    cfg.whitelist_count = 1;
    assert(mm2a_config_is_whitelisted(&cfg, "+989121112233"));
    assert(mm2a_config_is_whitelisted(&cfg, "09121112233"));
    assert(!mm2a_config_is_whitelisted(&cfg, "09359998877"));

    cfg.allow_all_callers = TRUE;
    assert(mm2a_config_is_whitelisted(&cfg, "09359998877"));

    mm2a_config_free(&cfg);
    g_print("[PASS] Config and whitelist tests passed successfully.\n");
    return 0;
}
