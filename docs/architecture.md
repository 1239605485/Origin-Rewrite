# Origin Rewrite architecture notes

## Authority boundaries

The native adapter owns only observation and application:

- observe the post-vanilla NPC baseline;
- identify a verified activation transition;
- pass a value object to the pure core;
- apply the returned snapshot once;
- report a verified death/loot ordering.

The pure core owns all decisions. It never calls PatchLib, allocates native objects, spawns an item, or assumes a Terraria method exists.

| Concern | Pure-core owner | Native adapter responsibility |
|---|---|---|
| Eligibility and one-shot roll | `or_spawn.c` | supply host, source, mode, progress and baseline |
| Stats | `or_stats.c` | read/write only verified fields |
| Rule snapshot | `or_rules.c`, `or_world.c` | supply world/terrain/weather values and persist state |
| AI budget | `or_ai.c` | translate a plan into compatible existing AI actions |
| State identity | `or_state.c` | associate `OR_InstanceKey` with the live NPC slot |
| Reward policy | `or_loot.c` | call original loot exactly once and resolve one extra slot |
| Vanilla item safety | `or_item_registry.c` | fill the target-version ID table |
| Method/field ABI | `or_runtime.c` | provide exact signatures before installing hooks |

## Spawn transaction

`or_spawn_try_commit()` is a transaction boundary. It checks authority and exclusions, snapshots rules, rolls the overall chance once, rolls the tier once, computes stats once, then reserves a slot generation and commits the complete state. A failed commit cleans the pending record and does not increment the active elite count.

The state table has an independent `active_elites` count. `npcSlots` is returned as a stat for the game's spawn-capacity calculation; it is not used as a substitute for the number of active elite records.

## Reward transaction

The death adapter should call `or_state_mark_death()` exactly once. After the original drop ordering is known, `or_loot_commit()` obtains the tier and terrain from the saved state, not from the current NPC position or caller-provided values. This prevents dragging an elite into another biome from changing its crate and prevents a stale slot from receiving a reward.

`OR_LootResult.pool_id` is a resolver key rather than an item ID. A later target-version registry must select exactly one enabled vanilla candidate and reject future content, Boss bags, Boss summons, exclusive Boss drops and key progression items. An empty or unverified registry is a safe no-op for the extra slot.

## ABI gate

`or_runtime_signature_matches()` checks:

- instance versus static method;
- exact return `patch_type_t`;
- exact argument count;
- exact argument types from the TEF vector;
- the method handle's validity.

`or_runtime_field_matches()` checks the instance flag, type and byte size. A missing optional field is represented by a null capability; it is not guessed or replaced by a method lookup based only on parameter count.
