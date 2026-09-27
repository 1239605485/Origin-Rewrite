#ifndef ORIGINREWRITE_VISUAL_POLICY_H
#define ORIGINREWRITE_VISUAL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

bool or_visual_effect_should_draw(bool npc_active, int32_t npc_life,
                                 bool death_started);
void or_visual_effect_light_tint(uint8_t out_rgb[3], uint8_t *out_alpha,
                                 const uint8_t light_rgb[3]);

#endif
