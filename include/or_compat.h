#ifndef ORIGINREWRITE_COMPAT_H
#define ORIGINREWRITE_COMPAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Checks whether the Unity family is known to the PatchLib adapter. */
bool or_compat_unity_supported(const char *unity_version);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_COMPAT_H */
