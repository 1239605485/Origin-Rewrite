# 新版维护约定

先读 README.md、docs/STATUS.md 和 docs/R0-AUDIT.md，按当前真实状态继续。不要把文档中的后续阶段当作已完成。

- 旧源码用于功能和接口取证；旧巨型 adapter/runtime 不得整体迁入。复用 SDK、资源和文案时记录来源。
- domain 只使用数据，不依赖 PatchLib、Android、文件或托管句柄。
- platform 负责探测和执行；玩法决定不能进入原生字段读写封装。
- app 装配模块并显式调度，私有状态由所属模块管理。
- NPC 标识必须包含世界会话、真实槽位和生成代次。缺少可信生成信号时不得开始反复概率抽取。
- Boss 战斗对话保留独立开关。不得新增 Boss AI、攻击机制或属性强化。
- 下一步先做 R2 的一个精英等级和提交/补偿闭环，再接规则、AI 和奖励，不一次性接入全部旧功能。
- 明确写入前与部分写入后的失败处理；不能在原生写入部分失败后声称游戏完全没有被改变。
- 不猜游戏签名、结构体布局、返回缓冲区大小或 RVA。SetDefaults 的值类型 Hook 未验证，不能直接启用。
- 修改跨模块行为后运行 make all、相关必要测试和 scripts/validate.py。真机结果与模拟测试分开报告。
- Android 包不得包含 host.so，发布包必须通过 AArch64 ELF 校验。
- 更新版本时同步 Info.json 和 include/or_version.h，源码压缩包及安装包均不增加额外根文件夹。
- 没有设备验证就保持 stableVerified=false；不得补写虚构日志或 PASS。
