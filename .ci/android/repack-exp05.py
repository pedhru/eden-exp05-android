#!/usr/bin/env python3
"""Package the rebuilt EXP05 native core with the supplied EXP04 frontend."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import zipfile


BASE_SHA256 = '655571ffbf20ef6060c936867cca8e62eca354d967a7cd884a39211919c9b642'


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def length(data, offset, utf8):
    if utf8:
        n = data[offset]
        return (((n & 127) << 8) | data[offset + 1], offset + 2) if n & 128 else (n, offset + 1)
    n = struct.unpack_from('<H', data, offset)[0]
    return (((n & 32767) << 16) | struct.unpack_from('<H', data, offset + 2)[0], offset + 4) if n & 32768 else (n, offset + 2)


def encode_length(n, utf8):
    if utf8:
        return bytes([(n >> 8) | 128, n & 255]) if n > 127 else bytes([n])
    return struct.pack('<HH', (n >> 16) | 32768, n & 65535) if n > 32767 else struct.pack('<H', n)


def patch_manifest(data):
    kind, header_size, size = struct.unpack_from('<HHI', data)
    assert kind == 3 and size == len(data)
    chunks = []
    offset = header_size
    strings = None
    pool_index = None
    while offset < len(data):
        chunk_kind, chunk_header, chunk_size = struct.unpack_from('<HHI', data, offset)
        assert chunk_size >= chunk_header >= 8 and offset + chunk_size <= len(data)
        chunk = bytearray(data[offset:offset + chunk_size])
        if chunk_kind == 1:
            assert strings is None
            count, style_count, flags, start, style_start = struct.unpack_from('<IIIII', chunk, 8)
            utf8 = bool(flags & 256)
            offsets = struct.unpack_from('<' + 'I' * count, chunk, chunk_header)
            strings = []
            for rel in offsets:
                n, pos = length(chunk, start + rel, utf8)
                if utf8:
                    n, pos = length(chunk, pos, True)
                    strings.append(bytes(chunk[pos:pos + n]).decode('utf-8'))
                else:
                    strings.append(bytes(chunk[pos:pos + n * 2]).decode('utf-16-le'))
            pool_index = len(chunks)
        chunks.append(chunk)
        offset += chunk_size
    assert strings is not None
    new_strings = ['nx2-nce-exp05-sx2a-0.3.0', 'Eden SX2A NCE EXP05']
    version_name, label = len(strings), len(strings) + 1
    strings += new_strings
    changed = set()
    for chunk in chunks:
        kind, chunk_header, _ = struct.unpack_from('<HHI', chunk)
        if kind != 0x102:
            continue
        element = strings[u32(chunk, chunk_header + 4)]
        attr_start, attr_size, attr_count = struct.unpack_from('<HHH', chunk, chunk_header + 8)
        assert attr_size >= 20
        for i in range(attr_count):
            pos = chunk_header + attr_start + i * attr_size
            name = strings[u32(chunk, pos + 4)]
            if element == 'manifest' and name == 'package':
                assert strings[u32(chunk, pos + 8)] == 'dev.eden.nx2'
            if element == 'manifest' and name == 'versionCode':
                old = u32(chunk, pos + 16)
                assert old == 34192005
                struct.pack_into('<I', chunk, pos + 8, 0xffffffff)
                chunk[pos + 15] = 0x10
                struct.pack_into('<I', chunk, pos + 16, 34192006)
                changed.add(name)
            elif (element == 'manifest' and name == 'versionName') or (element == 'application' and name == 'label'):
                value = version_name if name == 'versionName' else label
                struct.pack_into('<I', chunk, pos + 8, value)
                chunk[pos + 15] = 3
                struct.pack_into('<I', chunk, pos + 16, value)
                changed.add(name)
    assert changed == {'versionCode', 'versionName', 'label'}
    old = chunks[pool_index]
    _, pool_header, _ = struct.unpack_from('<HHI', old)
    count, style_count, flags, start, style_start = struct.unpack_from('<IIIII', old, 8)
    utf8 = bool(flags & 256)
    style_offsets = old[pool_header + count * 4:pool_header + (count + style_count) * 4]
    styles = old[style_start:] if style_start else b''
    payload = bytearray()
    offsets = []
    for s in strings:
        offsets.append(len(payload))
        encoded = s.encode('utf-8' if utf8 else 'utf-16-le')
        units = len(s.encode('utf-16-le')) // 2
        payload += encode_length(units, utf8)
        if utf8:
            payload += encode_length(len(encoded), True)
        payload += encoded + (b'\0' if utf8 else b'\0\0')
    payload += b'\0' * (-len(payload) % 4)
    start = pool_header + len(strings) * 4 + len(style_offsets)
    new_pool = bytearray(old[:pool_header])
    # Appending strings invalidates the sorted-string flag, but preserves all existing indices.
    struct.pack_into('<IIIII', new_pool, 8, len(strings), style_count, flags & ~1,
                     start, start + len(payload) if styles else 0)
    new_pool += struct.pack('<' + 'I' * len(offsets), *offsets) + style_offsets + payload + styles
    struct.pack_into('<I', new_pool, 4, len(new_pool))
    chunks[pool_index] = new_pool
    result = bytearray(data[:header_size]) + b''.join(chunks)
    struct.pack_into('<I', result, 4, len(result))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    assert hashlib.sha256(args.base.read_bytes()).hexdigest() == BASE_SHA256
    core = (args.native / 'libyuzu-android.so').read_bytes()
    assert b'Unsupported NX2 NCE instruction' in core and b'NX2 CPU backend:' in core
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.base) as source, zipfile.ZipFile(args.out, 'w', allowZip64=False) as target:
        assert source.testzip() is None
        for item in source.infolist():
            if re.fullmatch(r'META-INF/(MANIFEST.MF|[^/]+\.(SF|RSA|DSA|EC))', item.filename, re.I):
                continue
            data = source.read(item)
            if item.filename == 'AndroidManifest.xml':
                data = patch_manifest(data)
            elif item.filename == 'lib/arm64-v8a/libyuzu-android.so':
                data = core
            elif item.filename.startswith('lib/arm64-v8a/') and (args.native / Path(item.filename).name).is_file():
                data = (args.native / Path(item.filename).name).read_bytes()
            item.extra = b''
            target.writestr(item, data)
    print(args.out)


if __name__ == '__main__':
    main()
