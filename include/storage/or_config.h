#ifndef OR_CONFIG_H
#define OR_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
typedef struct { bool boss_dialog, diagnostics; } OR_Config;
OR_Config or_config_default(void);
bool or_config_parse(const char *text, OR_Config *out);
bool or_config_load(const char *private_dir, OR_Config *out, bool *found);
#endif
