#!/usr/bin/env python3
"""Assemble independent A64 references and test the production NX2 helpers."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def text_section(path):
    data = path.read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * header[11])
                for i in range(header[12])]
    names_header = sections[header[13]]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    for section in sections:
        if names[section[0]:].split(b'\0', 1)[0] == b'.text':
            return data[section[4]:section[4] + section[5]]
    raise RuntimeError('Missing .text')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--clang', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    pairs = [
        ('ldapr x1,[x2]', 'ldar x1,[x2]'),
        ('ldapr w3,[sp]', 'ldar w3,[sp]'),
        ('ldaprb w4,[x5]', 'ldarb w4,[x5]'),
        ('ldaprh w6,[x7]', 'ldarh w6,[x7]'),
        ('ldapr xzr,[x9]', 'ldar xzr,[x9]'),
        ('pacia x1,x2', 'nop'), ('pacib x3,sp', 'nop'),
        ('autia x4,x5', 'nop'), ('autib x6,x7', 'nop'),
        ('pacda x8,x9', 'nop'), ('pacdb x10,x11', 'nop'),
        ('autda x12,x13', 'nop'), ('autdb x14,x15', 'nop'),
        ('paciza x3', 'nop'), ('autizb x4', 'nop'),
        ('xpaci x5', 'nop'), ('xpacd x6', 'nop'), ('xpaclri', 'nop'),
        ('paciasp', 'nop'), ('pacibsp', 'nop'),
        ('autiasp', 'nop'), ('autibsp', 'nop'),
        ('pacia1716', 'nop'), ('pacib1716', 'nop'),
        ('autia1716', 'nop'), ('autib1716', 'nop'),
        ('paciaz', 'nop'), ('pacibz', 'nop'), ('autiaz', 'nop'), ('autibz', 'nop'),
        ('retaa', 'ret'), ('retab', 'ret'),
        ('braa x4,x5', 'br x4'), ('brab x6,sp', 'br x6'),
        ('braaz x7', 'br x7'), ('brabz x8', 'br x8'),
        ('blraa x9,x10', 'blr x9'), ('blrab x11,sp', 'blr x11'),
        ('blraaz x30', 'blr x30'), ('blrabz x12', 'blr x12'),
        ('irg x3,x4', 'add x3,x4,#0'), ('irg sp,sp,x5', 'add sp,sp,#0'),
        ('gmi x7,x8,x9', 'orr x7,x9,#1'), ('gmi xzr,sp,xzr', 'nop'),
        ('addg x10,sp,#1008,#15', 'add x10,sp,#1008'),
        ('subg sp,x11,#16,#3', 'sub sp,x11,#16'),
        ('stg x0,[x1,#16]', 'nop'), ('st2g x0,[x1,#-16]', 'nop'),
        ('stg x0,[sp,#-4096]!', 'sub sp,sp,#1,lsl #12'),
        ('st2g x0,[x2],#4080', 'add x2,x2,#4080'),
        ('ldg x2,[x3,#16]', 'nop'),
    ]
    slow = ['pacga x0,x1,x2', 'pacga x3,x4,sp', 'ldraa x0,[x1,#-4096]',
            'ldrab x2,[sp,#4088]!', 'stzg x0,[x1,#-16]!',
            'stz2g x0,[x1],#32', 'stgp x3,x4,[sp,#16]',
            'stgp x5,x6,[x7,#-1024]!', 'stgp x8,x9,[x10],#1008']
    with tempfile.TemporaryDirectory(prefix='nx2-nce-') as temp:
        temp = Path(temp)
        asm = temp / 'oracle.s'
        asm.write_text('.text\n.arch armv8.5-a+memtag\n' +
                       '\n'.join(a + '\n' + b for a, b in pairs) + '\n' + '\n'.join(slow))
        subprocess.run([args.clang, '--target=aarch64-linux-gnu', '-c', str(asm),
                        '-o', str(temp / 'oracle.o')], check=True)
        data = text_section(temp / 'oracle.o')
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        include = temp / 'nx2_oracle.inc'
        include.write_text('static constexpr unsigned direct[][2] = {\n' +
                           ',\n'.join('{0x%08x,0x%08x}' % (words[i * 2], words[i * 2 + 1])
                                      for i in range(len(pairs))) + '\n};\n' +
                           'static constexpr unsigned slow[] = {' +
                           ','.join('0x%08x' % w for w in words[len(pairs) * 2:]) + '};\n')
        cmd = ['g++', '-std=c++23', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-I' + str(root / 'src'), '-I' + str(temp),
               str(root / 'src/tests/core/arm/nx2_nce.cpp'), '-o', str(temp / 'nx2-test')]
        if not Path('/proc/self/maps').exists():
            cmd.insert(1, '-DNX2_NCE_NO_LSAN')
        subprocess.run(cmd, check=True)
        subprocess.run([str(temp / 'nx2-test')], check=True)
        print(f'LLVM oracle: {len(pairs)} direct replacements; {len(slow)} emulated encodings')


if __name__ == '__main__':
    main()
