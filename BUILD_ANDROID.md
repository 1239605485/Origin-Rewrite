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

脚本固定使用 `arm64-v8a` 和 `android-24`，并在打包前检查四个根目录文件以及
`Resources/lib/libOriginRewrite.android.arm64.so` 是否存在。

## GitHub Actions

把本目录中的全部内容上传到仓库根目录，保留
`.github/workflows/android-arm64.yml`，然后在 Actions 中运行
`Build Origin Rewrite Android ARM64`。下载名为
`OriginRewrite-android-arm64-installable` 的工件后，直接将 ZIP 导入 TEFManager。

不能把包含外层 `OriginRewrite/` 文件夹的源码 ZIP 直接导入手机端；那种 ZIP
没有根目录 `Manifest.json`，也没有已编译的 Android ARM64 动态库。
