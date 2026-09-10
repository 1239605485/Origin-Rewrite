#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must point to the Android NDK}"
NDK_BUILD="${NDK_BUILD:-$ANDROID_NDK_HOME/ndk-build}"

if [[ ! -x "$NDK_BUILD" ]]; then
  echo "ndk-build not found or not executable: $NDK_BUILD" >&2
  exit 1
fi

VERSION="${ORIGINREWRITE_VERSION:-$(python3 - "$ROOT_DIR/Info.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    print(json.load(handle)["version"])
PY
)}"

WORK_DIR="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/originrewrite-build.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

cd "$ROOT_DIR"
"$NDK_BUILD" \
  NDK_PROJECT_PATH="$ROOT_DIR" \
  APP_BUILD_SCRIPT="$ROOT_DIR/Android.mk" \
  APP_ABI=arm64-v8a \
  APP_PLATFORM=android-24 \
  NDK_LIBS_OUT="$WORK_DIR/libs" \
  NDK_OUT="$WORK_DIR/obj"

PACKAGE_DIR="$WORK_DIR/OriginRewrite"
mkdir -p "$PACKAGE_DIR/Resources/lib" "$PACKAGE_DIR/Resources/config"
cp "$ROOT_DIR/Manifest.json" "$ROOT_DIR/Info.json" "$ROOT_DIR/OriginRewrite.json" "$PACKAGE_DIR/"
test -s "$ROOT_DIR/icon.png"
cp "$ROOT_DIR/icon.png" "$PACKAGE_DIR/icon.png"
cp -a "$ROOT_DIR/Resources/lang" "$PACKAGE_DIR/Resources/"
cp "$ROOT_DIR"/config/*.json "$PACKAGE_DIR/Resources/config/"
cp "$WORK_DIR/libs/arm64-v8a/libOriginRewrite.so" \
  "$PACKAGE_DIR/Resources/lib/libOriginRewrite.android.arm64.so"

mkdir -p "$ROOT_DIR/dist"
ARCHIVE="$ROOT_DIR/dist/OriginRewrite-v${VERSION}.zip"
ARCHIVE_TMP="$WORK_DIR/OriginRewrite-v${VERSION}.zip"
(cd "$PACKAGE_DIR" && zip -qr "$ARCHIVE_TMP" .)
cp "$ARCHIVE_TMP" "$ARCHIVE"
unzip -t "$ARCHIVE" >/dev/null
echo "Created $ARCHIVE"
