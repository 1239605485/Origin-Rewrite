# OriginRewrite v1.2.1 重构边界与验收记录

## 本次改动

- 新增纯逻辑模块 `src/core/or_boss_ai.c`，不依赖 PatchLib、托管对象或全局适配器。
- 新增 `tests/boss_ai_test.c`，覆盖白名单、阶段时序、速度增量上限、冷却与非法输入。
- `or_adapter.c` 负责读取游戏状态、核对配置/权威/字段能力、应用速度增量；Boss 策略不直接触碰游戏 API。
- Boss 默认仍不参与重构体抽取。`enableBossAI` 仅控制独立 Boss AI。
- 关闭 `enableBossAI` 会保留原版 Boss AI，并且不会改动普通敌怪功能。
- Boss 活跃时持续屏蔽环境/精英播报；Boss 阶段/死亡对白保留优先权。
- 玩家死亡重生期间先重置普通播报，再显示 Boss 败北行，避免重生播报覆盖。
- 脚下特效每帧验证 NPC 活跃/生命值；按 `Lighting.GetColor` 颜色调制。

## 当前行为

| 项目 | 当前实现 |
|---|---|
| 支持 NPC 类型 | 4、50、222、262、370、636、657、668 |
| AI 时序 | 预警 48 帧、施压 10 帧、恢复 36 帧、冷却 180 帧 |
| 执行内容 | 施压期每 3 帧尝试一次小幅速度增量，单次 X 不超过 0.8、Y 不超过 0.5 |
| 运行权限 | 仅明确识别到的单机；玩家死亡或位置无效时暂停 |
| 安全退路 | 身份不在白名单、Vector2 字段不可读写、状态输入非法时跳过 |
| 不接入范围 | 蠕虫、多部件 Boss、Boss 肢体、分节 NPC、额外弹幕和召唤物 |

## 检查结果

- Boss AI 单元测试：通过。
- Boss 播报锁定与视觉存活/光照策略单元测试：通过。
- 项目 `src` 和 TEFKernel API 的宿主 GCC C11 语法检查：通过。
- 项目 JSON 文件解析：通过。
- 本工作环境没有 CMake 或 Android NDK，因此未生成 Android ARM64 `.so`，也未进行设备内 Hook/战斗测试。包元数据保持实验状态。

## Android 设备验收顺序

1. 先备份一个单机世界，安装 v1.2.1。
2. 查看日志 `[BOSS_AI_GATE]`，确认 AI 开关启用，位置和速度字段可用。
3. 召唤白名单中的一个 Boss。日志应出现 `[BOSS_PRIORITY_LOCK]`，并出现 `[BOSS_AI_PHASE]` 的 telegraph、pressure、recovery 和 cooldown 阶段变化。
4. 继续观察 Boss 原版攻击、移动和死亡结算；验证其没有额外弹幕、召唤物或精英前缀。
5. 重新召唤该 Boss，确认状态从预警重新开始；再将 `enableBossAI` 设为 `false`，确认 Boss 回到原版表现。
6. 击杀普通怪物，确认原有强化、播报和掉落行为没有变化。`[FOOT_FX_DRAW]` 应含图块坐标与光照值；NPC 死亡后应立即停止绘制。

若 `[BOSS_AI_GATE]` 显示字段不可用或战斗中出现异常，先在模组私有配置中关闭 `enableBossAI`。该开关只回滚新增 Boss AI。
