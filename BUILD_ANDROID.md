# Origin Rewrite Android ARM64 构建说明

手机版 TEFManager 读取的是 ZIP 根目录中的 `Manifest.json`。因此需要区分：

- 源码工程包：包含 `CMakeLists.txt`、`src/`、`include/` 和 `third_party/`；
- 可安装包：只包含元数据和已编译库，不包含外层源码目录。

## 可安装包结构

```text
OriginRewrite-android-arm64.zip
├─ Manifest.json
├─ Info.json
├─ OriginRewrite.json
└─ Resources/
   └─ lib/
      └─ libOriginRewrite.android.arm64.so
```

## 本地构建

需要 CMake、Android SDK 和 Android NDK。环境变量使用 `ANDROID_NDK_HOME` 或
`ANDROID_NDK_ROOT` 指向 NDK 根目录：

```bash
bash scripts/package_android_arm64.sh
```

0.3.0-name-display-source 安装后，先确认游戏能稳定启动，再进入世界并等待生成普通敌怪，最后导出 TEFKernel 日志。重点查找
`[OR_DIAG] stat_write`：`vanillaLife` 是原版最大生命，`finalLife` 是计算值，
`readbackLifeMax` 和 `readbackLife` 是写入后的回读值。若 TEFManager 日志包中没有模组
输出，可使用 Android logcat 过滤 `OriginRewrite` 标签。

同时检查：

- `[MODULE_BEACON] version=0.3.0-name-display-source versionCode=2026090431`
- `[INIT_STAGE] config_done`
- `[INIT_STAGE] runtime_probe_done`
- `[ENTRY_PROBE] SetDefaults candidate=0 params=2 abi=int32,pointer verified=yes`
- `[SAFE_MODE] color/NewText/loot/special-AI skipped; name-only P0-C`
- `[OR_DIAG] setdefaults_pending type=...`
- `[OR_DIAG] stat_write ... readbackLifeMax=ok:...`
- `[OR_DIAG] ai_callback count=...`, `[NAME_SOURCE] property=FullName` 或 `TypeName`，`[NAME_WRITE] ... prefix=异化体 writeOk=... reason=...` and `Elite committed: concept=重构体 ...`

本版本会额外写入 `originrewrite_runtime.log`。如果 TEFManager 导出包仍缺少模组日志，
请从 Android logcat 过滤 `OriginRewrite`，或从 TEFKernel 日志目录提取该文件。

脚本固定使用 `arm64-v8a` 和 `android-24`，并在打包前检查四个根目录文件以及
`Resources/lib/libOriginRewrite.android.arm64.so` 是否存在。

## GitHub Actions

把本目录中的全部内容上传到仓库根目录，保留
`.github/workflows/android-arm64.yml`，然后在 Actions 中运行
`Build Origin Rewrite Android ARM64`。下载名为
`OriginRewrite-android-arm64-installable` 的工件后，直接将 ZIP 导入 TEFManager。

不能把包含外层 `OriginRewrite/` 文件夹的源码 ZIP 直接导入手机端；那种 ZIP
没有根目录 `Manifest.json`，也没有已编译的 Android ARM64 动态库。
