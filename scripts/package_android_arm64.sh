#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to your Android NDK directory}"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
python3 "$ROOT_DIR/scripts/validate.py"
"$ANDROID_NDK_HOME/ndk-build" NDK_PROJECT_PATH="$ROOT_DIR" \
    APP_BUILD_SCRIPT="$ROOT_DIR/Android.mk" NDK_APPLICATION_MK="$ROOT_DIR/Application.mk" \
    NDK_OUT="$WORK_DIR/obj" NDK_LIBS_OUT="$WORK_DIR/libs"
NATIVE_LIBRARY="$WORK_DIR/libs/arm64-v8a/libOriginRewrite.so"
python3 "$ROOT_DIR/scripts/validate.py" "$NATIVE_LIBRARY"
mkdir -p "$ROOT_DIR/dist"
python3 - "$ROOT_DIR" "$NATIVE_LIBRARY" <<'PY'
import json,sys,zipfile
from pathlib import Path
root=Path(sys.argv[1]); native=Path(sys.argv[2])
info=json.loads((root/'Info.json').read_text())
archive=root/'dist'/f'OriginRewrite-v{info["version"]}-android-arm64.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for name in ['Manifest.json','Info.json','OriginRewrite.json']:
        z.write(root/name,name)
    for p in (root/'Resources').rglob('*'):
        if p.is_file(): z.write(p,p.relative_to(root).as_posix())
    z.write(native,'Resources/lib/libOriginRewrite.android.arm64.so')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert 'Manifest.json' in z.namelist()
print(archive)
PY
