#include "or_visual_policy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint8_t rgb[3];
    uint8_t alpha;
    const uint8_t dark[3] = {0u, 0u, 0u};
    const uint8_t dim[3] = {32u, 18u, 6u};
    const uint8_t bright[3] = {255u, 255u, 255u};

    assert(!or_visual_effect_should_draw(false, 100, false));
    assert(!or_visual_effect_should_draw(true, 0, false));
    assert(!or_visual_effect_should_draw(true, 20, true));
    assert(or_visual_effect_should_draw(true, 20, false));

    or_visual_effect_light_tint(rgb, &alpha, dark);
    assert(rgb[0] == 0u && rgb[1] == 0u && rgb[2] == 0u && alpha == 0u);
    or_visual_effect_light_tint(rgb, &alpha, dim);
    assert(rgb[0] == 32u && rgb[1] == 18u && rgb[2] == 6u && alpha == 32u);
    or_visual_effect_light_tint(rgb, &alpha, bright);
    assert(rgb[0] == 255u && rgb[1] == 255u && rgb[2] == 255u && alpha == 255u);
    puts("visual_policy_test: ok");
    return 0;
}
