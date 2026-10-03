#include "domain/or_effects.h"
#include <math.h>
#include <string.h>
bool or_effects_merge(const OR_Contribution *input, size_t count, OR_Effects *out) {
    OR_Contribution sorted[32];
    if (!out || count > 32 || (count && !input)) return false;
    if (count) memcpy(sorted, input, count * sizeof(*input));
    for (size_t i=1; i<count; ++i) {
        OR_Contribution v=sorted[i]; size_t j=i;
        while (j && sorted[j-1].source > v.source) { sorted[j]=sorted[j-1]; --j; }
        sorted[j]=v;
    }
    OR_Effects result={1,1,1,1,1};
    const unsigned masks[]={OR_LIFE,OR_DAMAGE,OR_DEFENSE,OR_REWARD,OR_AI};
    for (size_t i=0; i<count; ++i) {
        const OR_Contribution *c=&sorted[i];
        if ((i && sorted[i-1].source == c->source) || (c->changed & ~c->allowed) || (c->changed & ~31u)) return false;
        const double vals[]={c->life,c->damage,c->defense,c->reward,c->ai};
        double *dst[]={&result.life,&result.damage,&result.defense,&result.reward,&result.ai};
        for (unsigned j=0; j<5; ++j) if (c->changed & masks[j]) {
            if (!isfinite(vals[j]) || vals[j] < 0 || vals[j] > 1000) return false;
            *dst[j] *= vals[j]; if (!isfinite(*dst[j])) return false;
        }
    }
    double *dst[]={&result.life,&result.damage,&result.defense,&result.reward,&result.ai};
    for (unsigned j=0; j<5; ++j) if (*dst[j] > 8) *dst[j]=8;
    *out=result; return true;
}
