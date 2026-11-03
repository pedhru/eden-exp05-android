// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include "core/arm/nce/nx2.h"
#include "nx2_oracle.inc"

#ifdef NX2_NCE_NO_LSAN
extern "C" int __lsan_is_turned_off() { return 1; }
#endif

using namespace Core::NCE;

struct TestMemory {
    std::array<unsigned char, 32768> data{};
    bool ReadBlock(std::uint64_t address, void* value, std::size_t size) {
        if (address > data.size() || size > data.size() - address) return false;
        std::memcpy(value, data.data() + address, size);
        return true;
    }
    bool WriteBlock(std::uint64_t address, const void* value, std::size_t size) {
        if (address > data.size() || size > data.size() - address) return false;
        std::memcpy(data.data() + address, value, size);
        return true;
    }
};

int main() {
    std::size_t checks{};
    const auto check = [&](bool condition) { assert(condition); ++checks; };
    for (const auto& pair : direct) {
        const auto patch = GetNx2Patch(pair[0]);
        check(patch.kind == Nx2PatchKind::Replace);
        check(patch.instruction == pair[1]);
    }
    for (const auto word : slow) check(GetNx2Patch(word).kind == Nx2PatchKind::Emulate);
    check(GetNx2Patch(0xd4000021).kind == Nx2PatchKind::None); // SVC #1
    check(GetNx2Patch(0xd503203f).kind == Nx2PatchKind::None); // YIELD

    TestMemory memory;
    std::array<std::uint64_t, 31> regs{};
    std::uint64_t sp{};
    Nx2RegisterState state{regs, sp};
    std::mt19937_64 random(0x4e58324e4345);
    for (int i = 0; i < 1000; ++i) {
        for (auto& r : regs) r = random();
        sp = random();
        const unsigned rd = random() % 32, rn = random() % 32, rm = random() % 32;
        const auto before = regs;
        const auto before_sp = sp;
        const auto expected = ((rn == 31 ? 0 : regs[rn]) ^ (rm == 31 ? sp : regs[rm])) &
                              0xffffffff00000000ULL;
        check(EmulateNx2Instruction(0x9ac03000U | (rm << 16) | (rn << 5) | rd,
                                   state, memory));
        for (unsigned r = 0; r < 31; ++r) check(regs[r] == (r == rd ? expected : before[r]));
        check(sp == before_sp);
    }
    for (const int offset : {-4096, -8, 0, 8, 4088}) {
        for (const bool wb : {false, true}) {
            const auto imm = static_cast<unsigned>(offset / 8) & 1023;
            const auto word = 0xf8200400U | ((imm & 511) << 12) | ((imm & 512) << 13) |
                              (static_cast<unsigned>(wb) << 11) | (1 << 5);
            regs.fill(0); regs[1] = 8192;
            const std::uint64_t value = random();
            check(memory.WriteBlock(8192 + offset, &value, 8));
            check(EmulateNx2Instruction(word, state, memory));
            check(regs[0] == value);
            check(regs[1] == (wb ? 8192 + offset : 8192));
            regs[1] = memory.data.size() + 8192;
            const auto before = regs;
            check(!EmulateNx2Instruction(word, state, memory));
            check(regs == before);
        }
    }
    for (const unsigned op : {1U, 3U}) {
        for (const unsigned mode : {1U, 2U, 3U}) {
            for (const int offset : {-4096, -16, 0, 16, 4080}) {
                memory.data.fill(0xa5); regs[1] = 8192;
                const auto imm = static_cast<unsigned>(offset / 16) & 511;
                const auto word = 0xd9200000U | (op << 22) | (imm << 12) | (mode << 10) | (1 << 5);
                const std::size_t address = mode == 1 ? 8192 : 8192 + offset;
                const std::size_t bytes = op == 3 ? 32 : 16;
                check(EmulateNx2Instruction(word, state, memory));
                for (std::size_t i = address - 1; i <= address + bytes; ++i)
                    check(memory.data[i] == (i >= address && i < address + bytes ? 0 : 0xa5));
                check(regs[1] == (mode == 2 ? 8192 : 8192 + offset));
            }
        }
    }
    for (const unsigned mode : {1U, 2U, 3U}) {
        for (const int offset : {-1024, -16, 0, 16, 1008}) {
            memory.data.fill(0xa5); sp = 8192; regs[3] = random(); regs[4] = random();
            const auto word = 0x68000000U | (mode << 23) |
                              ((static_cast<unsigned>(offset / 16) & 127) << 15) |
                              (4 << 10) | (31 << 5) | 3;
            const auto address = mode == 1 ? 8192 : 8192 + offset;
            check(EmulateNx2Instruction(word, state, memory));
            std::array<std::uint64_t, 2> result;
            check(memory.ReadBlock(address, result.data(), sizeof(result)));
            check(result[0] == regs[3] && result[1] == regs[4]);
            check(sp == (mode == 2 ? 8192 : 8192 + offset));
            check(memory.data[address - 1] == 0xa5 && memory.data[address + 16] == 0xa5);
        }
    }
    check(!EmulateNx2Instruction(0xf8200c21U, state, memory)); // LDRA writeback overlap
    check(!EmulateNx2Instruction(0x69800400U, state, memory)); // STGP writeback overlap
    std::printf("NX2 NCE: %zu checks passed (ASan + UBSan)\n", checks);
}
