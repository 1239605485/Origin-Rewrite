#ifndef ORIGINREWRITE_CONFIG_IO_H
#define ORIGINREWRITE_CONFIG_IO_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OR_ConfigIoReport {
    bool file_found;
    bool parsed;
    uint32_t overrides_applied;
    uint32_t invalid_values;
} OR_ConfigIoReport;

bool or_config_io_apply_json(const char *json,
                             OR_Config *config,
                             OR_ConfigIoReport *report);

/* Applies the supported general.json overlay from
 * <KernelLoader private_dir>/config/general.json, falling back to
 * <KernelLoader private_dir>/Resources/config/general.json. Unknown keys are
 * ignored; validation/clamping remains centralized in or_config_validate(). */
bool or_config_io_apply_private(const char *private_dir,
                                OR_Config *config,
                                OR_ConfigIoReport *report);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_CONFIG_IO_H */
