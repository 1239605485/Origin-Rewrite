# Origin Rewrite｜起源重构 BNM 架构版

`0.9.7-tefmanager-feature-enums` 是基于整合优化设计文档 v0.6 与
`OriginRewrite-v0.6.84-source` 重建的 Android ARM64 KernelLoader 模组。
工程使用官方 BNM 2.5.2 做 IL2CPP 元数据定位，以 TEFKernel PatchLib 作为唯一
Hook 所有者，并把概率、属性、规则、AI 预算、状态与奖励决策保留在可测试的纯 C 核心。

## 架构

| 层 | 职责 | 失败策略 |
|---|---|---|
| `src/core` | 概率、进度、三档层级、属性、规则、AI 状态机、奖励策略、实例生命周期 | 不依赖游戏运行时，可独立测试 |
| `src/platform/bnm` | 按名称解析 `Terraria.NPC/Main/Item`、字段和方法；作为 NPC 属性首选读写后端 | Unity/符号/元数据任一不匹配即 SAFE-OFF |
| `src/platform/tef` | PatchLib ABI 探测、Prefix/Postfix Hook、公告、日志、配置读取 | 每项能力独立降级 |
| `src/entry` | KernelLoader `create_kernel_mod()` 入口与组合根 | 不满足 P0 门槛时不启用生成改写 |

BNM 编译目标为 Unity `2021.3`。启动时先通过 PatchLib 读取
`UnityEngine.Application.unityVersion`，再用 xDL 检查 BNM 所需 IL2CPP 符号；只有
两道门槛都通过，才从 TEFKernel 已完成初始化的 IL2CPP 线程调用
`BNM::Loading::TrySetupByUsersFinder()`。BNM 不安装第二套原生 Hook。

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

未经目标手机完整 ABI/顺序验证的特殊 AI 原生动作、颜色写入、额外物品生成、世界
存档写入与客户端同步不会伪装成已完成；它们保留了核心策略、状态和能力探测，并按
[`docs/FEATURE_MATRIX.md`](docs/FEATURE_MATRIX.md) 中的状态安全关闭。

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

产物 `OriginRewrite-v0.9.7-tefmanager-feature-enums-android-arm64.zip` 可直接导入
TEFManager，ZIP 根目录就是 `Manifest.json`，不是再套一层源码目录。详细说明见
[`BUILD_ANDROID.md`](BUILD_ANDROID.md)。

## 日志验收

优先检查 KernelLoader 私有目录中的 `originrewrite_runtime.log`，或用 logcat 过滤
`OriginRewrite`：

- `[UNITY_PROBE]`：读取到的 Unity 版本；
- `[BNM_GATE]` / `[BNM_METADATA]`：BNM 是否通过门槛及字段/方法解析结果；
- `[ENTRY_PROBE]`：`SetDefaults`/`AI` 的精确 ABI；
- `[ROLL]`：基础概率、规则后概率、抽取与提交结果；
- `[OR_DIAG] stat_write`：计算值和写回读值；
- `[NAME_WRITE]`：名称前缀写入与回读结果；
- `[LIFECYCLE_HEALTH]`：pending、活动实例、提交和清理计数。

## 许可证

本工程以 AGPL-3.0-or-later 发布。BNM 2.5.2、xDL 与 KernelLoader mod API 是
MIT 许可组件，详见 `LICENSE`、`THIRD_PARTY_NOTICES.md` 及各 vendored 文件头。
