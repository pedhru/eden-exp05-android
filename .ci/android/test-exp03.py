#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--lz4-source', type=Path, required=True)
parser.add_argument('--zbic-source', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
temp = root / '.nx2-tests'
temp.mkdir(exist_ok=True)
environment = os.environ.copy()
environment['TMPDIR'] = str(temp)
flags = [] if Path('/proc/self/task').exists() else ['-DNX2_TEST_NO_LSAN']
with tempfile.TemporaryDirectory(dir=temp) as directory:
    directory = Path(directory)
    lz4 = directory / 'liblz4.so'
    subprocess.run(['gcc', '-O2', '-shared', '-fPIC', str(args.lz4_source.resolve() / 'lib/lz4.c'), '-o', str(lz4)], env=environment, check=True)
    for name, extra in [('nx2_improvements', [str(lz4), '-Wl,-rpath,' + str(directory), '-pthread']),
                        ('nso_zbic', ['-I' + str(args.zbic_source.resolve() / 'src')])]:
        binary = directory / name
        subprocess.run(['g++', '-std=c++23', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', *flags, '-I' + str(root / 'src'),
                        str(root / 'src/tests/standalone' / (name + '.cpp')), *extra,
                        '-o', str(binary)], env=environment, check=True)
        subprocess.run([str(binary)], env=environment, check=True)
