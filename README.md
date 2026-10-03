# OriginRewrite 架构重制验证版

版本：2.0.0-alpha.2。阶段：R0/R1 源码与本机验证完成，Android 构建及真机验收待完成。

**这是新版架构的第一阶段，不是旧版全部功能的替代成品。** 本版观察世界及 NPC 生命周期，接入独立 Boss 对话通道，不修改怪物属性、速度、伤害、掉落或世界结构。普通精英生成、规则、附加 AI、奖励和脚下特效按后续阶段接入。

## 已完成

- 入口只做装配；平台探测、Hook、快照读取、生命周期、对话、队列、配置各自独立。
- 世界会话、真实 NPC 槽位和生成代次组成实体身份；拒绝/已判断标记不会随普通回调重置。
- 只使用严格匹配的 `UpdateAudio`、`NPC.AI`、`NPC.OnSpawn` 和可选 `NPCLoot`，不拦截带结构体参数的 SetDefaults。
- 有界状态与队列、退出世界清理、重复卸载保护、可用时读取 NPC 数组发现失活对象。
- 保留旧版 19 行 Boss 文案数据，独立出现、半血、掉落边界及战中玩家死亡观察；`enableBossDialog` 开关独立控制队列。
- 适配上传的 TEFManager 设置格式：`private_dir/config.json` 中的 `schemaVersion` 和 `values`。
- 纯效果合并与字段权限验证已实现并测试，尚未接入真实精英玩法；目前的通用合并上限不是最终平衡参数。

## 本阶段的明确限制

Boss 对话基础链已经实现，但不宣称全部遭遇都完成验收。分节/多部位/成对 Boss 的完整遭遇归并留到 R6；当前对 13、125、126、134、398 这类高风险实体保守跳过整体击败对话，避免将单部位掉落当成整场胜利。出现与半血信息仍以本版实体身份观察，分裂产生的新实体可能需要后续遭遇归并。

“战中玩家死亡”指观察到 Boss 活跃时的玩家死亡，不是已确认伤害来源归因。重复文案计数只保存于本次世界会话，暂不跨游戏重启保存。

本版使用 Main.UpdateAudio 作为候选的零参数观察循环；其在目标真机上是否持续被调用、返回主菜单能否及时清理，必须以日志验证。任何必需签名或字段无法确认时，模块保持关闭。清理依赖真实状态，不将长期没有 AI 回调当成已消失。

## 构建

本机逻辑和平台模拟验证：

```bash
make all
make sanitize
python3 scripts/validate.py
```

`build/libOriginRewrite.host.so` 仅供本机入口/链接验证，不能放入 Android 启动器。

Android ARM64：设置 `ANDROID_NDK_HOME` 指向 NDK，然后执行：

```bash
bash scripts/package_android_arm64.sh
```

或者将源码根目录放入自己的 GitHub 仓库，运行 `OriginRewrite rebuild ARM64` 工作流；本次没有替用户创建或推送仓库，也没有运行远程 CI。工作流沿用上传源码中的 NDK 26.3.11579264 和构建方式，仍需一次真实运行验证。alpha.2 显式设置 SDK 初始化包为 `platform-tools`，修复上传日志中 `Failed to find package 'tools'` 导致的提前退出。指定 NDK 仍在下一步单独安装。

脚本会验证产物为 AArch64 ELF，再生成 `dist/OriginRewrite-v2.0.0-alpha.2-android-arm64.zip`。安装包根目录直接包含 Manifest.json、Info.json、OriginRewrite.json 和 Resources，不增加外层文件夹。

## 设置与测试

上传的 TEFManager 在“模组设置”中应识别“Boss 战斗对话”和“私有诊断日志”。保存后重新启动游戏确保生效；运行时本模组也会在可用观察循环中按游戏 tick 轮询配置。

本版使用与旧版相同的包名 `li06.originrewrite`，意图作为升级版本。测试时保留旧安装包备份，并停用旧版及 EliteMonsters，避免同类模组同时拦截 NPC 链路影响观察结果。

手工配置格式：

```json
{
  "schemaVersion": 1,
  "values": {
    "enableBossDialog": true,
    "enableDiagnostics": true
  }
}
```

第一次真机测试先检查进世界、退出世界和普通怪生成。日志位于模组私有目录 `originrewrite_rebuild.log`，每次加载重新写入，最多 3000 条。应能看到 BUILD、CONFIG、CAPABILITY、HOOK、READY、WORLD 和 HEALTH。

再测试单体 Boss 的出现、半血和击败，关闭对话后重新测试；不应再展示该通道文字，普通原版战斗行为保持原有调用。普通精英不会生成，是本阶段预期行为。

发生闪退或完全没有观察消息时，提供启动器日志和私有日志；不要把“SDK 已注入”和“在手机上运行成功”混为一谈。见 [接口审计](docs/R0-AUDIT.md) 与 [交付状态](docs/STATUS.md)。

## 下一阶段

R2 接入普通精英资格判断、单次抽取、属性提交和失败补偿，先完成一个等级的真实可玩闭环，再扩展到三档和规则。Boss 对话继续作为表现模块，不承担精英生成来源判断。
