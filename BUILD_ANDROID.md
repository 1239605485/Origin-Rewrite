# Android ARM64 构建与安装

## 环境

- Android NDK r26c；
- Android API 24 或更高。

工程为 C11 纯 C 模组，只使用 TEFKernel PatchLib；构建目标固定为 `arm64-v8a`、API 24，
所以安装包只支持 64 位 ARM Android。

## 一键构建

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26c
bash scripts/package_android_arm64.sh
```

脚本会调用 `ndk-build`、生成 ARM64 动态库并组装可直接导入 TEFManager 的 ZIP。

## 安装包布局

```text
OriginRewrite-v1.0.27-name-broadcast-color-fix-arm64.zip
├── Manifest.json
├── Info.json
├── OriginRewrite.json
└── Resources/
    ├── lib/libOriginRewrite.android.arm64.so
    ├── config/*.json
    ├── lang/zh-CN.json
    ├── lang/en-US.json
    └── docs/
```

TEFManager 安装时会把 `Resources` 内容放入模组私有目录，运行时读取
`config/general.json`。如修改配置，请保持标准 JSON；解析失败会保留编译时安全默认值。

## 目标机验收

1. 先备份存档并在单机测试世界加载。
2. 确认 `[UNITY_PROBE]`、`[ENTRY_PROBE]` 和 `[COLOR_ABI_GATE]`；任何未验证能力都应 SAFE-OFF。
3. 确认 `[ENTRY_PROBE]` 同时接受 `SetDefaults(int32,pointer)` 和无参 `AI()`。
4. 生成多个普通敌怪，核对 `[ROLL]`、`stat_write`、`[NAME_READBACK]`、`[COLOR_WRITE]`、名称前缀和活动上限。
5. 观察 `[BROADCAST_COMMIT]` 的 messageId 是否递增，以及 `[TERRAIN_BROADCAST_COMMIT]` 是否出现。
6. 额外掉落和终焉体特殊 AI 应保持关闭；多人客户端暂不作为验收目标。

未经日志验证前不要打开 `feature_fallbacks.json` 所标识的危险原生能力。当前安装包的
`stableVerified=false` 是有意设置，表示已经由 GitHub Actions 编译和静态检查，但未在你的具体 APK/设备上
完成真机回归。

仓库中的 `.github/workflows/build-android.yml` 会自动执行 JSON 校验、宿主 C 语法检查、Android ARM64
构建和 ZIP 打包；也可以在 Actions 页面手动运行 `workflow_dispatch`。
