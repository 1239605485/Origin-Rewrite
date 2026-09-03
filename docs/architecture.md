# Origin Rewrite architecture notes

## Authority boundaries

The native adapter owns only observation and application:

- observe the post-vanilla NPC baseline;
- identify a verified activation transition;
- pass a value object to the pure core;
- apply the returned snapshot once;
- report a verified death/loot ordering.

The current P0 Android adapter installs only an exact-ABI `NPC.SetDefaults`
observation postfix and an exact-ABI parameterless `NPC.AI()` postfix.
`SetDefaults(int,pointer)` records a pending baseline only; the first AI callback
that reads `active=true` is the generation submission boundary. `NPC.NPCLoot()`
and color/`Main.NewText` calls are probed but not invoked. After the real commit,
the adapter obtains the original display name from a strictly checked
`FullName`/`TypeName` getter when `GivenName` is empty, then attempts one
`GivenName` write using the player-facing 重构体 prefix.
A pending
object that never activates is reclaimed by the AI grace path or object-slot
reuse, while a committed live record is protected until a verified lifecycle
cleanup boundary.

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

`or_spawn_try_commit()` is a transaction boundary. It checks authority and exclusions, snapshots rules, rolls the overall chance once, rolls the tier once, computes stats once, then reserves a slot generation and commits the complete state. `SetDefaults` does not call this function. The AI activation callback supplies the final baseline and invokes it once; a failed commit cleans the pending binding and does not increment the active elite count.

The adapter's verified AI callback is the minimum generation gate. The AI
state-machine actions, loot hook, and visual calls remain disabled until their
complete ABI and lifecycle are validated. Missing optional hooks must disable
only their own behavior, never the verified health/damage/defense overlay.

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

`SetDefaults` is discovered by parameter count only as a prefilter; the current
target profile accepts only the exact instance/void/(int32,pointer) signature
reported by the target mobile runtime. The pointer parameter is not interpreted
by the mod; it is used only as part of the ABI gate.
`AI()` is installed only after the exact instance/void/zero-argument signature
check. `NPCLoot()` remains a diagnostic-only probe and still requires the
complete signature before any future installation. Extra item spawning remains
disabled until `Item.NewItem` and the target-version vanilla item registry are
both verified.

## Player-facing naming

The display-layer concept is **重构体** (reconstruction entity). Its three
verified tier labels are:

| Internal tier key | Display prefix |
|---|---|
| `altered` | `异化体·` |
| `calamity` | `灾变体·` |
| `apocalypse` | `终焉体·` |

Internal enum and persistence keys remain unchanged. The name marker is written
once after the first real `active=true` commit and is read back before the
runtime log reports success. Color, chat, loot, and special AI remain separate
gated capabilities. If neither display-name getter is available, the adapter
falls back to the tier prefix alone and records that downgrade.
