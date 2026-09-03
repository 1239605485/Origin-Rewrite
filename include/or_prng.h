#ifndef ORIGINREWRITE_PRNG_H
#define ORIGINREWRITE_PRNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OR_Prng {
    uint64_t state;
} OR_Prng;

void or_prng_seed(OR_Prng *rng, uint64_t seed);
uint64_t or_prng_next_u64(OR_Prng *rng);
float or_prng_next_unit(OR_Prng *rng);
bool or_prng_chance(OR_Prng *rng, float probability);
size_t or_prng_weighted_index(OR_Prng *rng, const float *weights, size_t count);

#endif /* ORIGINREWRITE_PRNG_H */
