#include "or_config_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OR_CONFIG_FILE_LIMIT (64u * 1024u)

static const char *find_value(const char *json, const char *key) {
    char token[96];
    const char *position;
    const char *colon;
    if (!json || !key || snprintf(token, sizeof(token), "\"%s\"", key) <= 0) {
        return NULL;
    }
    position = strstr(json, token);
    if (!position) return NULL;
    colon = strchr(position + strlen(token), ':');
    if (!colon) return NULL;
    ++colon;
    while (*colon && isspace((unsigned char)*colon)) ++colon;
    return colon;
}

static bool read_bool_key(const char *json, const char *key,
                          bool *target, OR_ConfigIoReport *report) {
    const char *value = find_value(json, key);
    if (!value) return false;
    if (strncmp(value, "true", 4u) == 0) {
        *target = true;
    } else if (strncmp(value, "false", 5u) == 0) {
        *target = false;
    } else {
        report->invalid_values += 1u;
        return false;
    }
    report->overrides_applied += 1u;
    return true;
}

static bool read_double_key(const char *json, const char *key,
                            double *target, OR_ConfigIoReport *report) {
    const char *value = find_value(json, key);
    char *end = NULL;
    double parsed;
    if (!value) return false;
    errno = 0;
    parsed = strtod(value, &end);
    if (value == end || errno == ERANGE) {
        report->invalid_values += 1u;
        return false;
    }
    *target = parsed;
    report->overrides_applied += 1u;
    return true;
}

static bool read_file(const char *path, char **out) {
    FILE *file;
    long length;
    char *buffer;
    size_t read_length;
    if (!path || !out) return false;
    *out = NULL;
    file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0L ||
        (size_t)length > OR_CONFIG_FILE_LIMIT || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    buffer = (char *)malloc((size_t)length + 1u);
    if (!buffer) {
        fclose(file);
        return false;
    }
    read_length = fread(buffer, 1u, (size_t)length, file);
    fclose(file);
    if (read_length != (size_t)length) {
        free(buffer);
        return false;
    }
    buffer[read_length] = '\0';
    *out = buffer;
    return true;
}

bool or_config_io_apply_json(const char *json,
                             OR_Config *config,
                             OR_ConfigIoReport *report) {
    double value;
    OR_ConfigIoReport local = {0};
    if (!json || !config || !report) return false;
    *report = local;
    (void)read_bool_key(json, "enableElites", &config->enable_elites, report);
    (void)read_bool_key(json, "enableGameplayHooks",
                        &config->enable_gameplay_hooks, report);
    (void)read_bool_key(json, "enableBosses",
                        &config->eligibility.allow_bosses, report);
    (void)read_bool_key(json, "allowPreHardmodeApocalypse",
                        &config->allow_pre_hardmode_apocalypse, report);
    (void)read_bool_key(json, "apocalypseOnlyTest",
                        &config->apocalypse_only_test, report);
    (void)read_bool_key(json, "allowVanillaCrates",
                        &config->loot.allow_vanilla_crates, report);
    (void)read_bool_key(json, "enableNativeExtraLoot",
                        &config->loot.enable_native_extra_item, report);
    if (read_double_key(json, "maxActiveRewrites", &value, report) &&
        value >= 0.0 && value <= 4294967295.0) {
        config->max_active_elites = (uint32_t)value;
    }
    if (read_double_key(json, "sameNpcCooldownTicks", &value, report) &&
        value >= 0.0) {
        config->same_npc_cooldown_ticks = (uint64_t)value;
    }
    if (read_double_key(json, "journeyProbabilityMultiplier", &value, report)) {
        config->journey_probability_multiplier = (float)value;
    }
    if (read_double_key(json, "alteredExtraRewardChance", &value, report)) {
        config->loot.altered_extra_reward_chance = (float)value;
    }
    if (read_double_key(json, "maxExtraRewardSlots", &value, report) &&
        value >= 0.0 && value <= 255.0) {
        config->loot.max_extra_reward_slots = (uint8_t)value;
    }
    if (read_double_key(json, "classicChance", &value, report)) {
        config->modes[OR_MODE_CLASSIC].elite_chance = (float)value;
    }
    if (read_double_key(json, "expertChance", &value, report)) {
        config->modes[OR_MODE_EXPERT].elite_chance = (float)value;
    }
    if (read_double_key(json, "masterChance", &value, report)) {
        config->modes[OR_MODE_MASTER].elite_chance = (float)value;
    }
    if (read_double_key(json, "zenithChance", &value, report)) {
        config->modes[OR_MODE_ZENITH].elite_chance = (float)value;
    }
    if (read_double_key(json, "journeyChance", &value, report)) {
        config->modes[OR_MODE_JOURNEY].elite_chance = (float)value;
    }
    report->parsed = report->invalid_values == 0u;
    return true;
}

bool or_config_io_apply_private(const char *private_dir,
                                OR_Config *config,
                                OR_ConfigIoReport *report) {
    char path[768];
    char fallback_path[768];
    char *json = NULL;
    bool applied;
    if (!config || !report) return false;
    memset(report, 0, sizeof(*report));
    if (!private_dir || !*private_dir ||
        snprintf(path, sizeof(path), "%s/config/general.json", private_dir) <= 0 ||
        snprintf(fallback_path, sizeof(fallback_path),
                 "%s/Resources/config/general.json", private_dir) <= 0) {
        return false;
    }
    if (!read_file(path, &json) && !read_file(fallback_path, &json)) return false;
    applied = or_config_io_apply_json(json, config, report);
    report->file_found = true;
    free(json);
    return applied;
}
