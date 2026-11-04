#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and exercise the actual inline NX2 sparse state functions (C++20)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
COMPILER = shutil.which('g++') or shutil.which('clang++')
if not COMPILER:
    raise SystemExit('ERRO: instale g++ ou clang++ para executar o teste NX2')

SOURCE = r"""
#include "video_core/nx2.h"
#include <cassert>
#include <cstdint>
#include <limits>
int main() {
    using namespace Tegra;
    constexpr u64 start = 0x100000;
    constexpr u64 tile = Nx2SparseTileSize;
    Nx2ClearSparseRegions();
    Nx2SetSparseBacking({});
    Nx2SparseRegionCreated(start, 0, tile);
    Nx2SparseRegionCreated(start, tile, 0);
    Nx2SparseRegionCreated((std::numeric_limits<u64>::max)() - 1, tile, tile);
    assert(g_nx2_sparse_state.empty());

    Nx2SparseRegionCreated(start, 3 * tile, tile);
    Nx2AddSparseSpan(start, 3 * tile);
    assert(Nx2InSparseRegion(start + tile, tile));
    assert(!Nx2InSparseRegion(start + 2*tile, 2*tile));
    assert(Nx2SparseUnwritten(start, 3*tile));
    Nx2SparseBlockWritten(start, 99999999); // untrusted index, must not write out-of-bounds
    assert(Nx2SparseUnwritten(start, 3*tile));
    Nx2SparseBlockWritten(start, 1);
    assert(!Nx2SparseUnwritten(start, 3*tile));
    auto ranges = Nx2SparseDataRanges(start, 3*tile);
    assert(ranges.size() == 1 && ranges[0].first == tile && ranges[0].second == tile);
    assert(Nx2SparseDataRanges(start, (std::numeric_limits<u64>::max)()).empty());
    assert(Nx2SparseDataRanges(start + 3*tile, tile).empty());

    Nx2SparseTilesMapped(start, 4*tile, tile, true); // no change
    assert(g_nx2_sparse_state.at(start).mapped.empty());
    Nx2SparseTilesMapped(start, 2*tile, (std::numeric_limits<u64>::max)(), true); // clip
    ranges = Nx2SparseDataRanges(start, 3*tile);
    assert(ranges.size() == 1 && ranges[0].first == 2*tile && ranges[0].second == tile);
    Nx2SparseTilesMapped(start, 2*tile, tile, false);
    assert(Nx2SparseDataRanges(start, 3*tile).empty());
    Nx2SparseTilesMapped(start, 0, 2*tile, true);
    ranges = Nx2SparseDataRanges(start, 3*tile);
    assert(ranges.size() == 1 && ranges[0].first == 0 && ranges[0].second == 2*tile);

    Nx2SparseMarkWritten(start + 2*tile, tile);
    assert(!Nx2SparseUnwritten(start + 2*tile, tile));
    Nx2SparseMarkWritten((std::numeric_limits<u64>::max)()-2, 999999);
    assert(!Nx2InSparseRegion(start + tile, (std::numeric_limits<u64>::max)()));
    Nx2ClearSparseRegions();
    assert(g_nx2_sparse_state.empty());
    Nx2SetSparseBacking({});
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix='eden-nx2-bounds-') as d:
    base = Path(d)
    stub = base / 'common' / 'common_types.h'
    stub.parent.mkdir(parents=True)
    stub.write_text('#pragma once\n#include <cstdint>\nusing u64 = std::uint64_t;\nusing u32 = std::uint32_t;\n')
    (base / 'test.cpp').write_text(SOURCE)
    out = base / 'test'
    flags = ['-std=c++20', '-O0', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
    if os.environ.get('EDEN_NX2_SANITIZE') == '1':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run([COMPILER, *flags, '-I', str(base), '-I', str(ROOT / 'src'),
                    str(base / 'test.cpp'), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('PASSOU: NX2 sparse region bounds, overflows, mapped tiles, written bits, reset')
