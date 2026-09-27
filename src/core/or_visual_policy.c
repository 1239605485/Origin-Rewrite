#include "or_visual_policy.h"

bool or_visual_effect_should_draw(bool npc_active, int32_t npc_life,
                                 bool death_started) {
    return npc_active && npc_life > 0 && !death_started;
}

void or_visual_effect_light_tint(uint8_t out_rgb[3], uint8_t *out_alpha,
                                 const uint8_t light_rgb[3]) {
    uint8_t alpha;
    if (!out_rgb || !out_alpha || !light_rgb) return;
    out_rgb[0] = light_rgb[0];
    out_rgb[1] = light_rgb[1];
    out_rgb[2] = light_rgb[2];
    alpha = light_rgb[0] > light_rgb[1] ? light_rgb[0] : light_rgb[1];
    if (light_rgb[2] > alpha) alpha = light_rgb[2];
    *out_alpha = alpha;
}
