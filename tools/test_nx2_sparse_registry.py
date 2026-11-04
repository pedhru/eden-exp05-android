#!/usr/bin/env python3
"""Teste C++20 da classe REAL Nx2GpuMemoryRegistry extraída do cabeçalho.

Isola a classe sem deps Android e verifica replay, remapeamento parcial,
destruição e Reset. Não substitui testes Vulkan/SMMU no dispositivo.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / 'src/core/hle/service/nvdrv/devices/nx2_stub_device.h').read_text()
start = header.index('class Nx2GpuMemoryRegistry {')
end = header.index('// Device node', start)
registry_class = header[start:end]
assert registry_class.strip().endswith('};'), 'classe Nx2GpuMemoryRegistry não encontrada'
assert 'std::vector<Mapping> sparse_tiles' in registry_class, 'estado sparse não registrado'
assert 'TrimMappings(sparse_tiles, gpu_address, size)' in registry_class, 'trim sparse ausente'
assert 'sparse_tiles.clear();' in registry_class, 'Reset não limpa os tiles'

compiler = shutil.which('g++') or shutil.which('clang++')
if not compiler:
    raise SystemExit('ERRO: g++ ou clang++ necessário para o teste real C++20')

cpp = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
using u64 = std::uint64_t;
namespace Service::Nvidia::Devices {
""" + registry_class + r"""
}
using namespace Service::Nvidia::Devices;
using Entry = std::tuple<u64, u64, u64>;
int main() {
    auto& reg = Nx2GpuMemoryRegistry::Get();
    reg.Reset();
    reg.CreateSparse(0x1000, 0x3000);
    reg.BackSparse(0x1000, 0xA000, 0x1000);
    reg.MapSparseTile(0x2000, 0xB000, 0x1000);
    reg.MapSparseTile(0x2400, 0, 0x400); // desmap parcial

    std::vector<Entry> replay;
    std::vector<std::tuple<u64,u64,bool>> regions;
    int owner1{};
    reg.AddListener(&owner1, [](u64,u64,u64){}, [](u64,u64){}, {},
        [&](u64 address, u64 size, bool create) {
            regions.emplace_back(address,size,create);
        },
        [&](u64 gpu, u64 cpu, u64 size) {
            replay.emplace_back(gpu,cpu,size);
        });
    assert(regions.size() == 1);
    assert(regions[0] == std::make_tuple(u64{0x1000},u64{0x3000},true));
    // Verifica REPLAY de blocos para novo listener, inclusive offsets CPU corretos.
    assert(replay.size() == 3);
    assert(replay[0] == Entry(0x1000,0xA000,0x1000));
    assert(replay[1] == Entry(0x2000,0xB000,0x400));
    assert(replay[2] == Entry(0x2800,0xB800,0x800));

    reg.MapSparseTile(0x2200,0xC000,0x200);
    std::vector<Entry> replay2;
    int owner2{};
    reg.AddListener(&owner2, [](u64,u64,u64){}, [](u64,u64){}, {},
        [](u64,u64,bool){},
        [&](u64 gpu,u64 cpu,u64 size){ replay2.emplace_back(gpu,cpu,size); });
    assert(replay2.size() == 4);
    assert(replay2[0] == Entry(0x1000,0xA000,0x1000));
    assert(replay2[1] == Entry(0x2000,0xB000,0x200));
    assert(replay2[2] == Entry(0x2800,0xB800,0x800));
    assert(replay2[3] == Entry(0x2200,0xC000,0x200));

    reg.RemoveListener(&owner1);
    reg.RemoveListener(&owner2);
    reg.DestroySparse(0x1000,0x3000);
    std::vector<Entry> after_destroy;
    int owner3{};
    reg.AddListener(&owner3, [](u64,u64,u64){}, [](u64,u64){}, {},
        [](u64,u64,bool){},
        [&](u64 a,u64 b,u64 c){ after_destroy.emplace_back(a,b,c); });
    assert(after_destroy.empty());
    reg.RemoveListener(&owner3);
    reg.CreateSparse(0x5000,0x1000);
    reg.BackSparse(0x5000,0xD000,0x1000);
    reg.Reset();
    std::vector<Entry> after_reset;
    int owner4{};
    reg.AddListener(&owner4, [](u64,u64,u64){}, [](u64,u64){}, {},
        [](u64,u64,bool){},
        [&](u64 a,u64 b,u64 c){ after_reset.emplace_back(a,b,c); });
    assert(after_reset.empty());
    reg.RemoveListener(&owner4);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='eden-nx2-test-') as tmp:
    source = Path(tmp) / 'registry_test.cpp'
    binary = Path(tmp) / 'registry_test'
    source.write_text(cpp)
    subprocess.run([compiler, '-std=c++20', '-O0', '-Wall', '-Wextra', '-pthread',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASSOU: C++20 replay, unmap parcial, remap, destroy e reset do registro NX2')
