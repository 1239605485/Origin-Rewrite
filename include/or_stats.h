#ifndef ORIGINREWRITE_STATS_H
#define ORIGINREWRITE_STATS_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OR_StatsInput {
    const OR_Config *config;
    OR_VanillaStats vanilla;
    OR_ProgressStage progress;
    OR_GameMode mode;
    OR_EliteTier tier;
    OR_RuleSnapshot rules;
    bool suppress_body_scale;
} OR_StatsInput;

bool or_stats_apply(const OR_StatsInput *input, OR_FinalStats *out_stats);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_STATS_H */
