# OriginRewrite v1.2.5–v1.2.6 隔离诊断记录

## v1.2.6 第二阶段：通用 AI Hook 开启、Boss 状态机关闭

- v1.2.5 的运行日志确认版本为 1.2.5，AI_HOOK_ISOLATION installed=no、P0_GATE 关闭精英提交、HOOK_STATE gameplay=off；用户反馈可以进入游戏。
- v1.2.6 恢复通用 NPC.AI Hook 与精英提交，只在 update_boss_ai 入口强制返回。预期 AI_HOOK_ISOLATION installed=yes、BOSS_AI_GATE effective=no、HOOK_STATE gameplay=on，但不应有 BOSS_AI_PHASE。
- 若 v1.2.6 再现进图闪退，优先修复通用 Hook/回调边界或世界上下文读取；若稳定进入并且普通怪物正常，再在备份世界召唤支持的 Boss，验证状态机保持关闭后仍稳定。
- 此版本仍是诊断构建。游戏版本 1.4.5.8.6 高于声明的最高兼容版本 1.4.5.8.5；诊断结果不能作为该游戏版本正式兼容的依据。

## v1.2.5 NPC.AI Hook 隔离

- 运行日志显示 NPC.AI Hook 在首次 AI 回调中进入世界上下文读取后进程收到 SIGABRT。该证据尚不能区分通用 AI Hook、世界上下文读取或 Boss 状态逻辑。
- 诊断构建跳过 NPC.AI Hook 安装；保留 SetDefaults 捕获尝试，但由于精英唯一提交点依赖 AI 回调，P0 gate 会卸载 SetDefaults Hook 并关闭精英提交。
- Boss AI 阶段、普通精英提交以及依赖 AI 回调的运行功能在该构建中均不可用。启动日志必须包含 [AI_HOOK_ISOLATION] installed=no 与 [HOOK_STATE] version=1.2.5 gameplay=off。
- 若仍然闪退，说明关闭 AI Hook 后问题仍存在；若可以进地图，下一版本再单独恢复通用 AI Hook、继续保持 Boss 状态机关闭，以区分 Hook ABI/回调与 Boss 专用逻辑。
- 该构建用于一次进图隔离测试，不用于正常游玩；目标游戏 1.4.5.8.6 仍高于当前已声明的最高兼容版本 1.4.5.8.5。

## 本次改动

- 根据 v1.2.3 运行日志，启动在 `[WORLD_ID_PROBE]` 后中断，且未进入 Boss AI。移除可选静态 `Main.time` 字段的解析与读取；世界日计数使用 `Main.dayTime` 边沿，读取失败时使用 `GameUpdateCount`。
- 修复版需要设备确认：日志应在 `[WORLD_ID_PROBE]` 后继续出现 `[WORLD_CLOCK]`、`[WORLD_RULES]` 与 `[MODULE_BEACON] stage=ready`。

- Boss encounter 活跃时，非 Boss 敌怪不会进入重构抽取；这是对内核缺少可靠召唤者来源标记的保守隔离。
- `Main.worldID` 改为调用静态 getter `Main.get_worldID()` 读取；世界规则存档文件名按世界 ID 生成，避免不同世界共享固定回退 ID 和同一存档文件。
- 新增规则抽取 2–4 条及 Boss encounter 抽取拦截的纯逻辑回归测试。
- 新增纯逻辑模块 `src/core/or_boss_ai.c`，不依赖 PatchLib、托管对象或全局适配器。
- 新增 `tests/boss_ai_test.c`，覆盖白名单、阶段时序、速度增量上限、冷却与非法输入。
- `or_adapter.c` 负责读取游戏状态、核对配置/权威/字段能力、应用速度增量；Boss 策略不直接触碰游戏 API。
- Boss 默认仍不参与重构体抽取。`enableBossAI` 仅控制独立 Boss AI。
- 关闭 `enableBossAI` 会保留原版 Boss AI，并且不会改动普通敌怪功能。
- Boss 活跃时持续屏蔽环境/精英播报；Boss 阶段/死亡对白保留优先权。
- 玩家死亡重生期间先重置普通播报，再显示 Boss 败北行，避免重生播报覆盖。
- 脚下特效每帧验证 NPC 活跃/生命值；按 `Lighting.GetColor` 颜色调制。
- EyeBoostRemastered 分析只确认了 stripped 原生库的 Hook 范围和读取字段，不能恢复其私有 AI 实现；v1.2.3 的阶段循环是独立设计。
- 参考 [tModLoader ExampleMod 的状态机写法](https://github.com/tModLoader/tModLoader/blob/1.4.5/ExampleMod/Content/NPCs/ExampleCustomAISlimeNPC.cs)；针对手机保留原版 Boss AI 和弹幕，仅增加低频、受限的移动压力。不扫描 NPC 列表、不额外生成实体。
- EyeBoostRemastered 也挂钩 `NPC.AI`；同时启用两个模组的 Boss AI 会叠加位移效果。建议只启用其中一套；保留 EyeBoost 属性改动时，可在 OriginRewrite 设置中关闭 `enableBossAI`。

## 当前行为

| 项目 | 当前实现 |
|---|---|
| 支持 NPC 类型 | 4、50、222、262、370、636、657、668 |
| AI 时序 | 各白名单 Boss 使用独立预警/施压/恢复/冷却时长；低血量会缩短预警与冷却 |
| 执行内容 | 预警阶段轻制动提示；施压阶段每 3 tick 尝试有上限位移；前期 Boss 低血量时每 2 tick，单次 X 不超过 0.8、Y 不超过 0.5 |
| 运行权限 | 仅明确识别到的单机；玩家死亡或位置无效时暂停 |
| 安全退路 | 身份不在白名单、Vector2 字段不可读写、状态输入非法时跳过 |
| 不接入范围 | 蠕虫、多部件 Boss、Boss 肢体、分节 NPC、额外弹幕和召唤物 |

前期 Boss 的基准时序（预警 / 施压 / 恢复 / 冷却，单位为 AI tick）：

| Boss | 时序 | 追击风格 |
|---|---:|---|
| 克苏鲁之眼 | 42 / 18 / 30 / 108 | 朝玩家做短冲刺；生命 ≤45% 后预警 36、冷却 81、每 2 tick 冲刺一次 |
| 史莱姆王 | 45 / 12 / 34 / 120 | 只给水平位移，保留原版跳跃与传送 |
| 世界吞噬者头部 | 54 / 12 / 36 / 132 | 只驱动头部水平追击，虫身与尾部保持原版 |
| 克苏鲁之脑 | 48 / 12 / 38 / 126 | 短距离追击，不接管原版幻象/爬行者攻击 |
| 蜂王 | 42 / 15 / 34 / 120 | 飞行方向追击，不生成额外蜂刺 |
| 骷髅王头部 | 54 / 12 / 42 / 144 | 只给头部水平追击，手臂保持原版 |
| 独眼巨鹿 | 60 / 14 / 48 / 156 | 较慢的水平压力，给手机版留反应时间 |

## 检查结果

- 5 个纯逻辑测试通过：Boss AI、播报优先级、视觉策略、世界规则数量/可复现性、Boss 战生成隔离。
- 项目 `src` 的 25 个 C 文件通过宿主 GCC C11 语法检查。
- JSON/YAML 静态解析通过，清理后的源码压缩包完整性检查通过。
- 尚未生成 Android ARM64 `.so`，也未进行设备内 Hook/战斗测试；目标设备验收后再调整实验版标记。

## Android 设备验收顺序

1. 先备份一个单机世界，安装 v1.2.4。
2. 查看日志 `[BOSS_AI_GATE]`，确认 AI 开关启用，位置和速度字段可用。
3. 召唤克苏鲁之眼。日志应出现 `[BOSS_PRIORITY_LOCK]`，并出现 `[BOSS_AI_PHASE]` 的 telegraph、pressure、recovery 和 cooldown 循环；用低血量阶段确认冲刺频率增加。再测试其他白名单 Boss，确认旧阶段时序没有变化。
4. 继续观察 Boss 原版攻击、移动和死亡结算；验证其没有额外弹幕、召唤物或精英前缀。
5. 重新召唤该 Boss，确认状态从预警重新开始；再将 `enableBossAI` 设为 `false`，确认 Boss 回到原版表现。
6. 击杀普通怪物，确认原有强化、播报和掉落行为没有变化。`[FOOT_FX_DRAW]` 应含图块坐标与光照值；NPC 死亡后应立即停止绘制。

若 `[BOSS_AI_GATE]` 显示字段不可用或战斗中出现异常，先在模组私有配置中关闭 `enableBossAI`。该开关只回滚新增 Boss AI。
