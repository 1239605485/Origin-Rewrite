#!/usr/bin/env python3
"""Check real integration contracts; do not advertise a host .so as Android."""
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
info = json.loads((root / 'Info.json').read_text())
header = (root / 'include/or_version.h').read_text()
assert f'"{info["version"]}"' in header
assert str(info['versionCode']) in header
assert info['stableVerified'] is False
manifest = json.loads((root / 'Manifest.json').read_text())
assert manifest['file'] == 'OriginRewrite.json'
assert json.loads((root / manifest['file']).read_text())['lib_name'] == 'OriginRewrite'
defaults = json.loads((root / 'Resources/config/default.json').read_text())
assert defaults['schemaVersion'] == 1
for setting in info['settings']:
    assert setting['defaultValue'] == defaults['values'][setting['key']]
    assert setting['type'] == 'SWITCH'
for folder in ('src/domain', 'include/domain'):
    for p in (root / folder).glob('*'):
        if p.is_file():
            assert not re.search(r'tefkernel/|patchlib_|android/', p.read_text()), p
for p in (root / 'src/platform/tef').glob('*.c'):
    assert 'patchlib_field_set_value(' not in p.read_text()
if len(sys.argv) > 1:
    data = Path(sys.argv[1]).read_bytes()
    assert len(data) > 64 and data[:4] == b'\x7fELF'
    assert data[4] == 2 and data[5] == 1
    assert int.from_bytes(data[18:20], 'little') == 183, 'not AArch64'
print('PASS: metadata/defaults, version identity, domain boundary, read-only native path')
