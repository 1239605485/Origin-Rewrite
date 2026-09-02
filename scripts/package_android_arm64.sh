#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="${ORIGINREWRITE_BUILD_DIR:-$project_root/build-android-arm64}"
output_zip="${ORIGINREWRITE_OUTPUT:-$project_root/OriginRewrite-android-arm64.zip}"
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
    -DORIGINREWRITE_BUILD_TESTS=OFF

"$cmake_bin" --build "$build_dir" --config Release --target OriginRewrite -j2

library_path="$(find "$build_dir" -type f -name 'libOriginRewrite.android.arm64.so' -print -quit)"
if [[ -z "$library_path" ]]; then
    echo "Expected ARM64 library was not produced: libOriginRewrite.android.arm64.so" >&2
    exit 3
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/originrewrite-package.XXXXXX")"
trap 'rm -rf -- "$stage_dir"' EXIT

mkdir -p "$stage_dir/Resources/lib"
cp "$project_root/Manifest.json" "$stage_dir/Manifest.json"
cp "$project_root/Info.json" "$stage_dir/Info.json"
cp "$project_root/OriginRewrite.json" "$stage_dir/OriginRewrite.json"
cp "$library_path" "$stage_dir/Resources/lib/libOriginRewrite.android.arm64.so"

mkdir -p "$(dirname "$output_zip")"
(cd "$stage_dir" && zip -qr -FS "$output_zip" .)

test "$(unzip -Z1 "$output_zip" | rg -c '^Manifest\.json$')" -eq 1
test "$(unzip -Z1 "$output_zip" | rg -c '^Info\.json$')" -eq 1
test "$(unzip -Z1 "$output_zip" | rg -c '^OriginRewrite\.json$')" -eq 1
test "$(unzip -Z1 "$output_zip" | rg -c '^Resources/lib/libOriginRewrite\.android\.arm64\.so$')" -eq 1

echo "Installable Android ARM64 package: $output_zip"
unzip -l "$output_zip"
