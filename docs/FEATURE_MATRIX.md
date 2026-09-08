# Feature matrix — 0.8.0-terrain-change-diagnostic

| Capability | Build state | Runtime gate / note |
|---|---|---|
| Android ARM64 KernelLoader module | Built | ELF AArch64, API 24, expected SONAME |
| BNM 2.5.2 metadata | Read-only validation | Unity 2021.3 ABI build + IL2CPP preflight + PatchLib core-field cross-check; BNM writes disabled |
| PatchLib hooks | Implemented | exact target signatures only |
| Pending → active spawn transaction | Implemented | SetDefaults + AI must both verify |
| 20/30/40/50% mode probabilities | Implemented | Journey uses Classic rules |
| Three tiers and one-shot stat write | Implemented | BNM primary, PatchLib fallback |
| Name prefixes and readback | Implemented | compatible GivenName/display-name properties |
| World/terrain/weather rule snapshot | Implemented with fallback | missing context uses conservative snapshot |
| World/elite notices | Capability-gated | supported Main.NewText overload only |
| AI plan and five-stage state machine | Implemented in core | native dash/projectile/summon/phase actions SAFE-OFF |
| Loot policy and idempotence | Implemented in core | extra native item spawning SAFE-OFF |
| Vanilla loot and NPC.value money | Preserved | no second coin grant |
| Color/glow/particles | SAFE-OFF | native value representation not verified |
| World rule persistence | Deterministic core present | no verified Terraria save API in supplied SDK |
| Multiplayer client sync | Authority rules present | no verified message transport in supplied SDK |
| Boss voice/UI/custom content | SAFE-OFF | bosses excluded and no verified UI/content API |

“Implemented” means compiled code plus host tests. It does not mean the user's exact Terraria APK has
completed device regression; `Info.json` therefore remains experimental and not stable-verified.
