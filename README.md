# OriginRewrite｜起源重构 v1.0 正式版

起源重构在保留泰拉瑞亚原版节奏的基础上，为世界加入“逐步失衡”的战斗规则：怪物会按世界阶段转化为异化体、灾变体或终焉体；世界会随机形成一组记忆中的规则；玩家进入不同地形、遭遇不同天气时，后续出现的重构体也会随之改变。

本模组面向 Terraria 手机版 1.4.x 的 Android ARM64 平台，仅支持单机。它只使用 TEFKernel PatchLib 与 C11 规则核心；不替换原版掉落表、不额外造币，也不依赖 BNM。

## 核心功能

| 系统 | 内容 |
|---|---|
| 三档重构体 | 异化体、灾变体、终焉体拥有随世界进度增长的生命、伤害、防御、体型与奖励档位。 |
| 世界规则 | 进入世界随机抽取 2–4 条规则，每 3 个游戏日重新抽取，并在本次会话中保留规则记忆。 |
| 地形与天气 | 地形、天气与昼夜会影响后续重构体的快照与兼容能力。 |
| 怪物 AI | 近战、远程、飞行、蠕虫与特殊单位获得冲锋、俯冲、远射、召唤、相位或狂暴等兼容动作。 |
| 掉落奖励 | 原版掉落和金币完整保留；重构体最多追加一个按当前阶段白名单挑选的奖励槽。 |
| 中文播报 | 重构体、世界规则、地形与天气均使用独立的显眼淡色中文播报。 |

## 已实现的核心行为

- `SetDefaults` 只记录 `PendingInit`，不抽取、不修改属性、不占用活动名额。
- 首个可验证的 `NPC.AI()` Postfix 且 `active=true` 才执行一次 `SpawnCommitted`。
- 实例键为 `worldSessionId + npcSlot + generationId`，防止槽位复用污染状态。
- 读取原版最终基准后一次性计算生命、伤害、防御、体型、击退、金币和 `npcSlots`。
- 正式基础概率：普通 20%、专家 30%、大师 40%、天顶 50%、旅途沿用普通；活动上限 8。
- 三档显示前缀：`异化体·`、`灾变体·`、`终焉体·`。
- 世界/地形/天气规则在生成提交点冻结为快照；进度无法确认时回退到较早阶段。
- AI 使用 `Ready → Telegraph → Active → Recovery → Cooldown` 状态机与硬上限。
- 奖励策略保留原版掉落、最多一个额外槽，并用一次性标志防重复结算。
- 只有主机或单机端能抽取与结算；客户端不会自行生成重构体或奖励。
- `Resources/config/general.json` 的开关、概率、活动上限和冷却会从 KernelLoader
  私有目录读取，非法值由统一验证器限幅。

怪物 AI 已按 v0.6 开放近战、远程、飞行、蠕虫和特殊体的完整动作层：原版 AI 先执行，
再按五阶段叠加位移、原生投射物、召唤、相位免伤和狂暴。投射物/召唤/免伤动作分别使用
运行时精确探测到的目标成员；目标版本缺少某个成员时，仅该动作回退为原版，不影响其他 AI。

## 参考模组迁移开关

本版本吸收了 EliteMonsters 的几个可靠做法：在 `NPCLoot` 后缀作为死亡边界、主机/单机
权限判断、每个实例只结算一次、`Item.NewItem` 返回槽位的回读验证，以及失败时保留原版
掉落。原生额外物品当前允许调用；数组可读时要求 `NewItem` 返回槽位的物品类型和堆叠
回读同时匹配，Android 数组不可读时使用已验证的 `NewItem` 返回槽位作为兼容验证；调用
失败或返回无效槽位时保留原版掉落且不伪造额外奖励成功。

名称前缀保留原名并登记显示路径；播报文本还带有 Terraria 原生 `[c/色值:文本]` 标记，
避免部分版本忽略 `Color` 结构体参数时所有播报变成同一种颜色。特殊 AI 已开放位移、
投射物、召唤和相位免伤动作，并在启动日志输出各工厂的解析结果。

播报颜色已按灾变体、终焉体、天气、地形、世界规则和 Boss 分组为显眼淡色，并会在日志
中输出 `[BROADCAST_COLOR]` 方便真机核对。GitHub Actions 工作流位于
`.github/workflows/build-android.yml`，每次推送会生成 ARM64 安装包。

## 构建与测试

宿主测试：

```bash
cmake -S . -B build-host -G Ninja -DORIGINREWRITE_BUILD_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Android ARM64 安装包：

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26c
bash scripts/package_android_arm64.sh
```

产物 `OriginRewrite-v1.0.0.zip` 可直接导入
TEFManager，ZIP 根目录就是 `Manifest.json`，不是再套一层源码目录。详细说明见
[`BUILD_ANDROID.md`](BUILD_ANDROID.md)。

## 日志验收

优先检查 KernelLoader 私有目录中的 `originrewrite_runtime.log`，或用 logcat 过滤
`OriginRewrite`：

- `[UNITY_PROBE]`：读取到的 Unity 版本；
- `[ENTRY_PROBE]`：`SetDefaults`/`AI` 的精确 ABI；
- `[ROLL]`：基础概率、规则后概率、抽取与提交结果；
- `[OR_DIAG] stat_write`：计算值和写回读值；
- `[NAME_WRITE]`：名称前缀写入与回读结果；
- `[LIFECYCLE_HEALTH]`：pending、活动实例、提交和清理计数。
- `deathPending` / `[LIFECYCLE_CLEANUP]`：死亡观察后的短暂保留和兜底清理；`elite` 只统计未进入死亡状态的绑定。

## 许可证

本工程以 AGPL-3.0-or-later 发布。xDL 与 KernelLoader mod API 是
MIT 许可组件，详见 `LICENSE`、`THIRD_PARTY_NOTICES.md` 及各 vendored 文件头。
