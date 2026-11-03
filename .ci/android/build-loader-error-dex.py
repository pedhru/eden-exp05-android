#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--jdk', type=Path, required=True)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
jdk, sdk, output = args.jdk.resolve(), args.sdk.resolve(), args.out.resolve()
classes, dex = output / 'classes', output / 'dex'
classes.mkdir(parents=True, exist_ok=True)
dex.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['JAVA_HOME'] = str(jdk)
env['PATH'] = str(jdk / 'bin') + ':' + env['PATH']
env['LD_LIBRARY_PATH'] = ':'.join([str(jdk / 'lib'), str(jdk / 'lib/server')])
env['JAVA_TOOL_OPTIONS'] = '-Djava.io.tmpdir=' + str(output)
android = sdk / 'platforms/android-36/android.jar'
source = root / 'src/android/app/src/main/java/org/yuzu/yuzu_emu/utils/Nx2LoadError.java'
subprocess.run([str(jdk / 'bin/javac'), '-source', '8', '-target', '8', '-classpath', str(android),
                '-d', str(classes), str(source)], env=env, check=True)
subprocess.run([str(jdk / 'bin/java'), '-cp', str(sdk / 'build-tools/35.0.0/lib/d8.jar'),
                'com.android.tools.r8.D8', '--release', '--min-api', '33', '--lib', str(android),
                '--output', str(dex), *(str(p) for p in classes.rglob('*.class'))], env=env, check=True)
print(dex / 'classes.dex')
