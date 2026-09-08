#include "or_compat.h"

#include <string.h>

static bool starts_with(const char *value, const char *prefix) {
    size_t length;
    if (!value || !prefix) return false;
    length = strlen(prefix);
    return strncmp(value, prefix, length) == 0;
}

bool or_compat_bnm_unity_supported(const char *unity_version) {
    return starts_with(unity_version, "2021.3.") ||
           starts_with(unity_version, "2022.2.") ||
           starts_with(unity_version, "2022.3.");
}
