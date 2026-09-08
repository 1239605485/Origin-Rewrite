#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="${ORIGINREWRITE_BUILD_DIR:-$project_root/build-android-arm64}"
output_zip="${ORIGINREWRITE_OUTPUT:-$project_root/OriginRewrite-v0.9.7-tefmanager-feature-enums-android-arm64.zip}"
cmake_bin="${CMAKE:-cmake}"

ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$ndk_dir" ]]; then
    echo "ANDROID_NDK_HOME or ANDROID_NDK_ROOT is required" >&2
    exit 2
fi

toolchain="$ndk_dir/build/cmake/android.toolchain.cmake"
if [[ ! -f "$toolchain" ]]; then
    echo "Android toolchain not found: $toolchain" >&2
    exit 2
fi

"$cmake_bin" -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_static \
    -DORIGINREWRITE_BUILD_TESTS=OFF

"$cmake_bin" --build "$build_dir" --config Release --target OriginRewrite -j2

library_path="$(find "$build_dir" -type f -name 'libOriginRewrite.android.arm64.so' -print -quit)"
if [[ -z "$library_path" ]]; then
    echo "Expected ARM64 library was not produced: libOriginRewrite.android.arm64.so" >&2
    exit 3
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/originrewrite-package.XXXXXX")"

mkdir -p "$stage_dir/Resources/lib" "$stage_dir/Resources/docs"
cp "$project_root/Manifest.json" "$stage_dir/Manifest.json"
cp "$project_root/Info.json" "$stage_dir/Info.json"
cp "$project_root/OriginRewrite.json" "$stage_dir/OriginRewrite.json"
cp "$library_path" "$stage_dir/Resources/lib/libOriginRewrite.android.arm64.so"
cp -R "$project_root/config" "$stage_dir/Resources/config"
cp -R "$project_root/Resources/lang" "$stage_dir/Resources/lang"
cp "$project_root/LICENSE" "$stage_dir/Resources/docs/LICENSE"
cp "$project_root/THIRD_PARTY_NOTICES.md" "$stage_dir/Resources/docs/THIRD_PARTY_NOTICES.md"
cp "$project_root/docs/FEATURE_MATRIX.md" "$stage_dir/Resources/docs/FEATURE_MATRIX.md"

mkdir -p "$(dirname "$output_zip")"
(cd "$stage_dir" && zip -qr -FS "$output_zip" .)

entry_count() {
    unzip -Z1 "$output_zip" | grep -Fxc -- "$1" || true
}

test "$(entry_count 'Manifest.json')" -eq 1
test "$(entry_count 'Info.json')" -eq 1
test "$(entry_count 'OriginRewrite.json')" -eq 1
test "$(entry_count 'Resources/lib/libOriginRewrite.android.arm64.so')" -eq 1
test "$(entry_count 'Resources/config/general.json')" -eq 1
test "$(entry_count 'Resources/lang/zh-CN.json')" -eq 1
test "$(entry_count 'Resources/lang/en-US.json')" -eq 1

echo "Installable Android ARM64 package: $output_zip"
unzip -l "$output_zip"
