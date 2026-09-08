#ifndef ORIGINREWRITE_CONFIG_H
#define ORIGINREWRITE_CONFIG_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void or_config_default(OR_Config *config);
bool or_config_validate(OR_Config *config);
OR_GameMode or_config_effective_stats_mode(OR_GameMode mode);
bool or_config_tier_allowed(const OR_Config *config,
                            OR_ProgressStage progress,
                            OR_GameMode mode,
                            OR_EliteTier tier);
const char *or_progress_stage_name(OR_ProgressStage stage);
const char *or_game_mode_name(OR_GameMode mode);
const char *or_elite_tier_name(OR_EliteTier tier);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_CONFIG_H */
