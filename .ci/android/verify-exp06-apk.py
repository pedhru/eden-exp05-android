#!/usr/bin/env python3
"""Verify the signed experimental APK and its JNI/dependency compatibility."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile


def run(args):
    return subprocess.check_output([str(a) for a in args], text=True)


def verify_elf(data):
    h = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    assert h[0][:6] == b'\x7fELF\x02\x01' and h[2] == 183
    assert h[6] + h[11] * h[12] <= len(data)
    assert h[5] + h[9] * h[10] <= len(data)
    for i in range(h[10]):
        ph = struct.unpack_from('<IIQQQQQQ', data, h[5] + h[9] * i)
        assert ph[2] + ph[5] <= len(data)
        if ph[0] == 1:
            assert ph[7] >= 0x4000


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--apk', type=Path, required=True)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--ndk', type=Path, required=True)
    p.add_argument('--build-tools', type=Path, required=True)
    p.add_argument('--java', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    tools = args.ndk / 'toolchains/llvm/prebuilt/linux-x86_64'
    signing = run([args.java, '-jar', args.build_tools / 'lib/apksigner.jar',
                   'verify', '--verbose', '--print-certs', args.apk])
    assert 'Verified using v3 scheme (APK Signature Scheme v3): true' in signing
    assert '9d09b2001be0dde93ea0444c536b72270fab3eb1a2fccfd56e1a3c46cc0d7af1' in signing
    run([args.build_tools / 'zipalign', '-c', '-P', '16', '4', args.apk])
    badging = run([args.build_tools / 'aapt2', 'dump', 'badging', args.apk])
    assert "name='dev.eden.nx2'" in badging
    assert "versionCode='34192007'" in badging
    assert "versionName='nx2-nce-exp06-sx2a-0.3.0'" in badging
    assert "minSdkVersion:'33'" in badging
    assert "application-label:'Eden SX2A NCE EXP06'" in badging
    assert "native-code: 'arm64-v8a'" in badging
    native = []
    needed = {}
    with zipfile.ZipFile(args.apk) as apk, zipfile.ZipFile(args.base) as base, tempfile.TemporaryDirectory(prefix='verify-nce-') as temp:
        temp = Path(temp)
        assert apk.testzip() is None
        assert not any('/.' in n for n in apk.namelist())
        native = [n for n in apk.namelist() if n.startswith('lib/') and n.endswith('.so')]
        packaged = {Path(n).name for n in native}
        for name in native:
            assert name.startswith('lib/arm64-v8a/')
            data = apk.read(name)
            verify_elf(data)
            path = temp / Path(name).name
            path.write_bytes(data)
            deps = re.findall(r'Shared library: \[(.*?)\]', run([tools / 'bin/llvm-readelf', '-d', path]))
            for dep in deps:
                assert dep in packaged or (tools / 'sysroot/usr/lib/aarch64-linux-android/33' / dep).exists(), dep
            needed[Path(name).name] = deps
        name = 'lib/arm64-v8a/libyuzu-android.so'
        core = apk.read(name)
        assert core != base.read(name)
        assert b'NX2 CPU backend:' in core
        assert b'Unsupported NX2 NCE instruction' in core
        original = temp / 'original.so'
        original.write_bytes(base.read(name))
        def jni(path):
            return set(re.findall(r'\b(Java_\w+)', run([tools / 'bin/llvm-nm', '-D', '--defined-only', path])))
        old_exports = jni(original)
        new_exports = jni(temp / 'libyuzu-android.so')
        assert old_exports and old_exports <= new_exports, sorted(old_exports - new_exports)
        for name in base.namelist():
            if name.endswith('.dex'):
                assert apk.read(name) == base.read(name)
            if name.startswith('assets/') or name.startswith('res/') or name == 'resources.arsc':
                assert apk.read(name) == base.read(name), name
    report = {
        'apk': args.apk.name,
        'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        'bytes': args.apk.stat().st_size,
        'package': 'dev.eden.nx2',
        'version_code': 34192007,
        'version': 'nx2-nce-exp06-sx2a-0.3.0',
        'minimum_android_api': 33,
        'native_abi': 'arm64-v8a',
        'jni_exports_preserved': len(old_exports),
        'native_libraries': native,
        'dependencies': needed,
        'signature_v3_verified': True,
        'zipalign_16k_verified': True,
        'elf_16k_alignment_verified': True,
        'crc_verified': True,
        'frontend_dex_unchanged': True,
        'resources_and_bundled_drivers_unchanged': True,
        'nce_core_changed_and_markers_present': True,
        'phone_or_game_runtime_tested': False,
    }
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
