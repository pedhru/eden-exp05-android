#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zlib-source', type=Path, required=True)
parser.add_argument('--vulkan-headers', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
temp = root / '.nx2-tests'
temp.mkdir(exist_ok=True)
environment = os.environ.copy()
environment['TMPDIR'] = str(temp)
sanitizers = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
flags = [] if Path('/proc/self/task').exists() else ['-DNX2_TEST_NO_LSAN']
zlib = args.zlib_source.resolve()
zlib_files = ['adler32.c', 'crc32.c', 'deflate.c', 'inflate.c', 'inftrees.c', 'inffast.c',
              'trees.c', 'zutil.c', 'compress.c', 'uncompr.c']
with tempfile.TemporaryDirectory(dir=temp) as directory:
    directory = Path(directory)
    library = directory / 'libexp04z.so'
    subprocess.run(['gcc', '-O1', '-g', *sanitizers, '-shared', '-fPIC', '-I' + str(zlib),
                    *(str(zlib / name) for name in zlib_files), '-o', str(library)],
                   env=environment, check=True)
    binary = directory / 'nx2_cache_metadata'
    subprocess.run(['g++', '-std=c++23', '-O1', '-g', *sanitizers, *flags,
                    '-I' + str(root / 'src'), '-I' + str(args.vulkan_headers.resolve() / 'include'),
                    '-I' + str(zlib), str(root / 'src/tests/standalone/nx2_cache_metadata.cpp'),
                    str(library), '-Wl,-rpath,' + str(directory), '-o', str(binary)],
                   env=environment, check=True)
    subprocess.run([str(binary), str(directory)], env=environment, check=True)
