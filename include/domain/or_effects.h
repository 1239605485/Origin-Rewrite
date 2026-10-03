#ifndef OR_EFFECTS_H
#define OR_EFFECTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { OR_LIFE=1, OR_DAMAGE=2, OR_DEFENSE=4, OR_REWARD=8, OR_AI=16 };
typedef struct {
    uint32_t source, allowed, changed;
    double life, damage, defense, reward, ai;
} OR_Contribution;
typedef struct { double life, damage, defense, reward, ai; } OR_Effects;
bool or_effects_merge(const OR_Contribution *input, size_t count, OR_Effects *out);
#endif
