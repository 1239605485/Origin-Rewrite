# Origin Rewrite configuration boundary

The v0.1 architecture keeps the validated v0.4 baseline in `src/or_config.c` so the core test binary has one deterministic source of truth. The resource JSON loader is intentionally not enabled yet: the supplied TEFKernel `mod-api` exposes PatchLib and package primitives, but this project has not verified the target game's resource path and world-data callbacks.

When the loader is added, these files are the only permitted ownership boundaries:

- `general.json`: module switches, mode probability, journey multiplier, active elite limit and exclusions.
- `tiers.json`: tier weights, progress × tier stats, scale, defense, money, knockback and NPC slots.
- `ai.json`: compatibility matrix, five-state timings and per-tier AI budget.
- `loot.json`: explicit vanilla item whitelist, quantities, crate mapping and safe fallbacks.
- `rules.json`: world, terrain, weather and event snapshot modifiers.

Until that loader is verified, changing a JSON file does not change runtime behavior. This avoids having a packaged file silently disagree with the compiled defaults.
