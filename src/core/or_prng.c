#include "or_prng.h"

#include <float.h>

static uint64_t or_mix_seed(uint64_t seed) {
    /* SplitMix64 gives slot/tick seeds a useful non-zero starting state. */
    seed += UINT64_C(0x9e3779b97f4a7c15);
    seed = (seed ^ (seed >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    seed = (seed ^ (seed >> 27)) * UINT64_C(0x94d049bb133111eb);
    return seed ^ (seed >> 31);
}

void or_prng_seed(OR_Prng *rng, uint64_t seed) {
    if (!rng) return;
    rng->state = or_mix_seed(seed);
    if (rng->state == 0) rng->state = UINT64_C(0x2545f4914f6cdd1d);
}

uint64_t or_prng_next_u64(OR_Prng *rng) {
    uint64_t x;
    if (!rng) return 0;
    x = rng->state;
    if (x == 0) x = UINT64_C(0x2545f4914f6cdd1d);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng->state = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

float or_prng_next_unit(OR_Prng *rng) {
    /* Use the high 24 bits so the result is exactly representable as float. */
    const uint32_t high = (uint32_t)(or_prng_next_u64(rng) >> 40);
    return (float)high / 16777216.0f;
}

bool or_prng_chance(OR_Prng *rng, float probability) {
    if (probability <= 0.0f) return false;
    if (probability >= 1.0f) return true;
    return or_prng_next_unit(rng) < probability;
}

size_t or_prng_weighted_index(OR_Prng *rng, const float *weights, size_t count) {
    size_t i;
    float total = 0.0f;
    float roll;

    if (!rng || !weights || count == 0) return SIZE_MAX;
    for (i = 0; i < count; ++i) {
        if (weights[i] > 0.0f) total += weights[i];
    }
    if (total <= FLT_EPSILON) return SIZE_MAX;

    roll = or_prng_next_unit(rng) * total;
    for (i = 0; i < count; ++i) {
        if (weights[i] <= 0.0f) continue;
        if (roll < weights[i]) return i;
        roll -= weights[i];
    }
    return count - 1;
}
