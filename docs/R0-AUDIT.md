# R0 接口与重写基线审计

依据仅来自本次提供的源码和附件；没有把原文的“正式版”“已生效”声明当作真机证据。附件 SHA-256 见 input-manifest.json。

## 输入角色

| 附件 | 实际用途 |
| --- | --- |
| OriginRewrite-v1.1.0-源代码(3).zip | 对照功能与状态问题；复用 SDK、许可证和静态对话文案 |
| Source-1.4.5.8.(1).zip | 核对 NPC/Main/Player/Entity 的声明和 NPCSpawnParams 值类型；方法体为空，不能证明内部逻辑 |
| TEFKernel-main(1)(1).zip | 核对 PatchLib 的签名、字段、FFI 和 Hook 实现 |
| TEFManager-main(7).zip | 核对设置元数据和私有 config.json 格式 |
| EliteMonsters-1.3.2-源代码...zip | 参考加载器元数据、生命周期和 NewText 路径；不搬入巨型 mod.c |
| 极速建造源码(2).zip | 参考 create_kernel_mod、logger 及 Entity.whoAmI 的正确所属类 |
| 源码.(5).zip | 抽样检查旧 EFMod/TEFMod 示例；API 属于另一体系，不直接复制到 KernelLoader |

## 接口决策

| 接口 | 附件证据 | 本版处理 |
| --- | --- | --- |
| create_kernel_mod | mod-api/mod_core.h、极速建造/main.c | 保留 ABI，入口独立实现 |
| Entity.whoAmI | Terraria/Entity.cs，极速建造 main.c 直接从 Entity 解析 | 从 Entity 获取字段，不依赖 NPC 的继承查找 |
| NPC.AI | NPC.cs 约 3450 行：实例 void、无参数 | 完整签名匹配后安装后置回调 |
| NPC.OnSpawn | NPC.cs 约 2732 行：实例 void、IEntitySource 参数 | 只匹配对象参数；观察新代次，不读取未知来源内部布局 |
| NPC.SetDefaults | NPC.cs 约 2738 行：int 与 NPCSpawnParams；后者为 struct | 首版不 Hook；OnSpawn 记录身份，首次 AI 只观察 |
| Main.UpdateAudio | Main.cs 约 10867 行：实例 void、无参数 | 作为观察循环候选，运行频率与菜单时机待真机验证 |
| Main.worldID | Main.cs 约 6289 行：只读静态属性 | 使用 get_worldID，返回值以 8 字节单元承接 |
| Main.GameUpdateCount | Main.cs 约 9358 行：静态 UInt32 属性 | 读取 getter，处理计数回绕 |
| Main.gameMenu | Main.cs 约 7618 行：静态 Boolean 属性 | 读取 getter，返回单元保持自然对齐 |
| Main.netMode | Main.cs 约 1855 行：静态 int 字段 | 直接验证静态字段，不虚构 get_netMode 方法 |
| Main.npc | Main.cs 约 1468 行：静态 NPC 数组 | 有条件读取，使用数组长度/索引 helper 检查失活；不写入 |
| Main.NewText | Main.cs 约 14210 行：string、三字节颜色、bool，共 5 参数 | 只使用精确 5 参数版本；不沿用其他游戏版本的 4 参数假设 |
| NPC.NPCLoot | NPC.cs 约 4462 行：实例 void、无参数 | 可选掉落边界，只观察；life<=0 后才进入对话结算 |

所有声明仍需要目标手机运行时元数据匹配。没有写死 RVA、对象偏移或游戏方法地址。没有依据空方法体推断来源、死亡归因或 Boss 归并。

## 发现的底层风险与处理

1. 内核 `src/patchlib/common/method.c` 将普通非标量值类型映射为 PATCH_POINTER；Android 通用 Hook 的 libffi 签名也按该枚举构造。不能由此证明 SetDefaults 的 NPCSpawnParams 按值参数正确，所以本版不使用该 Hook。动作工厂也暂不接入。
2. 内核 `src/patchlib/android/method_hook.c` 安装路径未完整传播 closure 创建/DobbyHook 失败。模组侧会验证签名和 SDK 返回状态，但不能保证识别内核内部所有安装失败；本轮没有改写内核。
3. 内核的字段读取 API 返回 void，无法证明读取成功。本版在确认字段大小/类型后使用其 get_pointer 判断空指针，再复制标量。
4. SDK 的标量 FFI 返回可能按原生字写入。采用对齐 8 字节返回单元，测试模拟了向 bool/UInt32 getter 写入 8 字节。
5. 缺少可信全场 Boss 遭遇和攻击来源数据时，不宣称准确识别整场胜利或击杀玩家归因；复杂遭遇结算保守停用。

## 功能矩阵

| 功能 | 旧版证据 | 新版状态 |
| --- | --- | --- |
| 三档精英 | 源码和配置存在 | R2/R3 待接入 |
| 世界/地形/天气规则 | 源码存在 | 独立贡献合并逻辑已实现；实际规则和采样待接入 |
| 普通精英五阶段 AI | 源码存在 | R4 待接入 |
| 原版掉落后的额外奖励 | 源码存在 | R5 待接入，无本版额外奖励调用 |
| 名称和脚下资源 | 源码/资源存在 | R5 待接入，保留旧附件作为资源来源 |
| Boss 静态文案 | 原文共 19 行 VOICE 数据 | 原样保留文案，重新实现观察和队列 |
| 对话独立开关 | 新需求；启动器设置 API 存在 | 已实现开关元数据、解析、队列清理及模拟测试 |
| 世界切换/槽位复用 | 源码存在相关处理 | 新实现，已做逻辑与模拟平台验证 |
| 手机实际运行 | 本轮没有设备结果 | 待验证，Info.json stableVerified=false |

## 复用登记

| 复用内容 | 新位置 | 理由 |
| --- | --- | --- |
| mod-api SDK 及函数指针导出文件 | mod-api/ | 保留加载器 ABI，非玩法架构复用 |
| 旧版 Boss 文案数据行 | src/presentation/boss_voices.inc | 保留玩家已有内容，不复制旧调度代码 |
| 旧版许可证 | LICENSE | 保持原项目许可 |
| 原构建方式与 NDK 版本约定 | Android.mk、构建工作流 | 重新列出新源文件；去除旧的业务源文件及硬编码版本不一致 |

生命周期、队列、观察器、配置解析、合并器、平台探测与应用调度均为本次新实现。没有迁入旧 or_adapter.c 或 or_runtime.c。
