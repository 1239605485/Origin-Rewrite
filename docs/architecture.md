# Origin Rewrite architecture notes

## Authority boundaries

The native adapter owns only observation and application:

- observe the post-vanilla NPC baseline;
- identify a verified activation transition;
- pass a value object to the pure core;
- apply the returned snapshot once;
- report a verified death/loot ordering.

The current crash-isolation Android adapter installs only the verified
`NPC.SetDefaults` postfix for stat application and readback. `NPC.AI()` and
`NPC.NPCLoot()` are probed but not installed, and name, color, and `Main.NewText`
calls are probed but not invoked. Every SetDefaults transaction is reclaimed
before the callback returns, because no AI/death hook is available to close a
long-lived binding yet. These are temporary safety gates, not claims that the
optional entry points are safe.

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

`or_spawn_try_commit()` is a transaction boundary. It checks authority and exclusions, snapshots rules, rolls the overall chance once, rolls the tier once, computes stats once, then reserves a slot generation and commits the complete state. During `SetDefaults`, the adapter uses a transient transaction so the returned stat snapshot is applied immediately like the verified reference mod; that temporary state is cleaned before the NPC enters the live pool. A failed commit cleans the pending record and does not increment the active elite count.

The adapter's `SetDefaults` stat overlay is the minimum gameplay gate. The AI
and loot hooks remain disabled until their complete ABI and lifecycle are
validated. Missing optional hooks must disable only their own behavior, never
the verified health/damage/defense overlay.

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

The only method discovered by parameter count without a complete argument-type
list is the known `SetDefaults` overload family. Every candidate is still
checked as an instance method before installation. `AI()` and `NPCLoot()` are
currently diagnostic-only probes; `NPCLoot()` still requires the complete
instance/void/zero-argument signature before any future installation. Extra
item spawning remains disabled until `Item.NewItem` and the target-version
vanilla item registry are both verified.
