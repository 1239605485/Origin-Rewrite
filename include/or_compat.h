#ifndef ORIGINREWRITE_COMPAT_H
#define ORIGINREWRITE_COMPAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BNM's Il2CppHeaders are selected at compile time. The supported runtime
 * families are explicitly gated here before BNM touches IL2CPP metadata. */
bool or_compat_bnm_unity_supported(const char *unity_version);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_COMPAT_H */
