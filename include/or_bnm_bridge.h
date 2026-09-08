#ifndef ORIGINREWRITE_BNM_BRIDGE_H
#define ORIGINREWRITE_BNM_BRIDGE_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OR_BnmCapabilities {
    bool compiled;
    bool unity_version_supported;
    bool il2cpp_symbols_ready;
    bool loaded;
    bool npc_metadata_ready;
    bool stats_read_ready;
    bool stats_write_ready;
    bool lifecycle_methods_ready;
    bool loot_method_ready;
} OR_BnmCapabilities;

/* Initializes the official BNM metadata runtime from the already initialized
 * IL2CPP thread owned by TEFKernel. The bridge never installs BNM native
 * hooks; PatchLib remains the single hook owner. */
bool or_bnm_bridge_init(const char *unity_version);
void or_bnm_bridge_shutdown(void);

const OR_BnmCapabilities *or_bnm_bridge_capabilities(void);
const char *or_bnm_bridge_status(void);

/* Primary metadata-backed NPC access. Callers must fall back to their verified
 * PatchLib accessors when these functions return false. */
bool or_bnm_read_npc(void *instance,
                     uint32_t *npc_type,
                     OR_VanillaStats *stats,
                     bool *is_boss,
                     bool *is_town,
                     bool *is_friendly,
                     bool *active);
bool or_bnm_write_npc_stats(void *instance, const OR_FinalStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_BNM_BRIDGE_H */
