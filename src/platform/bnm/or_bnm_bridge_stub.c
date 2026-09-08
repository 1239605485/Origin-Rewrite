#include "or_bnm_bridge.h"

static const OR_BnmCapabilities g_capabilities = {0};

bool or_bnm_bridge_init(const char *unity_version) {
    (void)unity_version;
    return false;
}

void or_bnm_bridge_shutdown(void) {}

const OR_BnmCapabilities *or_bnm_bridge_capabilities(void) {
    return &g_capabilities;
}

const char *or_bnm_bridge_status(void) {
    return "not-compiled-for-host";
}

bool or_bnm_read_npc(void *instance,
                     uint32_t *npc_type,
                     OR_VanillaStats *stats,
                     bool *is_boss,
                     bool *is_town,
                     bool *is_friendly,
                     bool *active) {
    (void)instance;
    (void)npc_type;
    (void)stats;
    (void)is_boss;
    (void)is_town;
    (void)is_friendly;
    (void)active;
    return false;
}

bool or_bnm_write_npc_stats(void *instance, const OR_FinalStats *stats) {
    (void)instance;
    (void)stats;
    return false;
}
