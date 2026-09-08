# Android ARM64 构建与安装

## 环境

- CMake 3.22.1 或更高；
- Ninja；
- Android NDK r26c；
- Android API 24 或更高。

BNM 需要 C++20，脚本固定使用 `arm64-v8a`、API 24 与静态 libc++，所以安装包只支持
64 位 ARM Android。

## 一键构建

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r26c
bash scripts/package_android_arm64.sh
```

也可指定输出位置：

```bash
ORIGINREWRITE_OUTPUT=/tmp/originrewrite.zip \
  bash scripts/package_android_arm64.sh
```

## 安装包布局

```text
OriginRewrite-v0.9.7-tefmanager-feature-enums-android-arm64.zip
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
2. 确认 `[UNITY_PROBE]` 与 `[BNM_GATE]`；BNM 若 SAFE-OFF，PatchLib 后端仍可运行。
3. 确认 `[ENTRY_PROBE]` 同时接受 `SetDefaults(int32,pointer)` 和无参 `AI()`。
4. 生成多个普通敌怪，核对 `[ROLL]`、`stat_write`、名称前缀和活动上限。
5. 再测试主机多人；客户端不应自行抽取或重复改写。

未经日志验证前不要打开 `feature_fallbacks.json` 所标识的危险原生能力。当前安装包的
`stableVerified=false` 是有意设置，表示已经由 GitHub Actions 编译和静态检查，但未在你的具体 APK/设备上
完成真机回归。
