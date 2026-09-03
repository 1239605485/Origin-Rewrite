# Origin Rewrite v1.0.1-diagnostics（全新框架·诊断可观测版）

这是从旧 OriginRewrite 实现之外重新搭的干净框架。当前只做“最薄完整核心链路”，
不含技能 AI、掉落、世界规则、地形天气、多人同步。

## 本版证明什么

1. 工程能被 GitHub Actions 编译并打包成手机可安装格式。
2. `NPC.SetDefaults` 只记录待初始化与基准，不抽取精英、不改属性。
3. 首次 `active=true` 的 `NPC.AI()` 回调才是提交点：一次 roll、一次应用属性。
4. 属性写入后立即回读并输出 `stat_write / readbackLifeMax`，可用 logcat 独立验证。

## 手机包格式（和之前能装机的完全一致）

```text
OriginRewrite-android-arm64.zip
├─ Manifest.json
├─ Info.json
├─ OriginRewrite.json
└─ Resources/
   └─ lib/
      └─ libOriginRewrite.android.arm64.so
```

打包脚本会自动生成上面这个 ZIP。GitHub Actions 的 artifact 是
`OriginRewrite-android-arm64-installable`，下载后直接导入 TEFManager。

不要导入源码 ZIP：源码目录里没有根目录 `Manifest.json` 和已编译的
`Resources/lib/*.so`，手机端会报“无法安装”。

## 在 Actions 上构建

把本目录内容作为仓库根目录上传（保留 `.github/workflows/android-arm64.yml`），
运行 `Build Origin Rewrite Android ARM64`，下载
`OriginRewrite-android-arm64-installable` 工件即可。

## 真机验证步骤

1. 安装后进入世界，等待普通敌怪生成。
2. 导出 TEFKernel 日志，或使用 Android logcat：

```text
adb logcat -s OriginRewrite
```

3. 确认以下关键行：

```text
[FW_DIAG] adapter_hooks setdefaults=2 ai=on gameplay=on
[FW_DIAG] setdefaults_pending type=... vanillaLife=...
[FW_DIAG] stat_write type=... tier=... finalLife=... writeOk=yes
          readbackLifeMax=ok:<finalLife> readbackLife=ok:...
```

`gameplay=on` 表示 SetDefaults 与 AI 提交点都已接入；
`stat_write` 且 `readbackLifeMax == finalLife` 表示属性确实写入了真实 NPC。

如果只有 `setdefaults_pending` 而没有 `stat_write`，说明 AI 提交点没触发，
应优先修 AI 入口，而不是继续叠功能。

## 后续接入顺序

本框架在 `fw_core.c` 中已经留好边界：

1. AI Postfix 内只做 tick 计数，后续在这里接有限状态机。
2. `NPCLoot` 只在签名确认时打印 ready，后续在这里接掉落。
3. 名称/颜色/血条等最小可观测标记下一步接入，替换“只看日志”的验证方式。
4. 概率、倍率当前是 `fw_roll.c` 的内建默认值，后续迁到 JSON 配置。
