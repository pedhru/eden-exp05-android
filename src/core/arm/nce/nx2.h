// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

namespace Core::NCE {

inline bool SupportsNx2Nce() {
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    return (getauxval(AT_HWCAP) & HWCAP_ATOMICS) != 0;
#else
    return false;
#endif
}

enum class Nx2PatchKind { None, Replace, Emulate };

struct Nx2Patch {
    Nx2PatchKind kind{};
    std::uint32_t instruction{};
};

namespace Nx2Detail {

struct Pattern {
    std::uint32_t mask{};
    std::uint32_t bits{};

    template <std::size_t N>
    consteval Pattern(const char (&pattern)[N]) {
        static_assert(N == 33);
        for (std::size_t i = 0; i < 32; ++i) {
            mask = (mask << 1) | (pattern[i] == '0' || pattern[i] == '1');
            bits = (bits << 1) | (pattern[i] == '1');
        }
    }

    constexpr bool Matches(std::uint32_t word) const {
        return (word & mask) == bits;
    }
};

inline constexpr Pattern Ldapr{"zz11100010111111110000nnnnnttttt"};
inline constexpr Pattern PacAut{"110110101100000100Zooonnnnnddddd"};
inline constexpr Pattern Xpac{"110110101100000101000D11111ddddd"};
inline constexpr Pattern Pacga{"10011010110mmmmm001100nnnnnddddd"};
inline constexpr Pattern Reta{"110101100101111100001M1111111111"};
inline constexpr Pattern Bra{"1101011Z00L1111100001Mnnnnnmmmmm"};
inline constexpr Pattern Ldra{"11111000MS1iiiiiiiiiW1nnnnnttttt"};
inline constexpr Pattern Stg{"11011001oo1iiiiiiiiixxnnnnnttttt"};
inline constexpr Pattern Stgp{"0110100xx0iiiiiiiuuuuunnnnnttttt"};
inline constexpr Pattern Irg{"10011010110mmmmm000100nnnnnddddd"};
inline constexpr Pattern Gmi{"10011010110mmmmm000101nnnnnddddd"};
inline constexpr Pattern Addg{"1001000110iiiiii00ttttnnnnnddddd"};
inline constexpr Pattern Subg{"1101000110iiiiii00ttttnnnnnddddd"};

constexpr std::int64_t SignExtend(std::uint32_t value, unsigned bits) {
    const auto sign = std::int64_t{1} << (bits - 1);
    return (static_cast<std::int64_t>(value) ^ sign) - sign;
}

constexpr std::uint32_t AddOffset(unsigned rd, unsigned rn, std::int64_t offset) {
    const auto magnitude = static_cast<std::uint32_t>(offset < 0 ? -offset : offset);
    const bool shift = magnitude == 4096;
    const auto imm = shift ? 1U : magnitude;
    return (offset < 0 ? 0xd1000000U : 0x91000000U) | (std::uint32_t{shift} << 22) |
           (imm << 10) | (rn << 5) | rd;
}

} // namespace Nx2Detail

// Match the JIT's unsigned-pointer, zero-allocation-tag model without using host PAC/MTE.
constexpr Nx2Patch GetNx2Patch(std::uint32_t word) {
    using namespace Nx2Detail;
    // Reject opcode families that contain none of the NX2 adaptations below.
    switch (word >> 24) {
    case 0x38: case 0x78: case 0xb8: case 0xf8:
    case 0xda: case 0x9a: case 0xd6: case 0xd7:
    case 0xd5: case 0xd9: case 0x68: case 0x69:
    case 0x91: case 0xd1:
        break;
    default:
        return {};
    }
    const auto rd = word & 31;
    const auto rn = (word >> 5) & 31;
    constexpr auto nop = 0xd503201fU;
    const auto replace = [](std::uint32_t instruction) {
        return Nx2Patch{Nx2PatchKind::Replace, instruction};
    };
    const auto emulate = [word] { return Nx2Patch{Nx2PatchKind::Emulate, word}; };

    if (Ldapr.Matches(word)) {
        return replace(0x08dffc00U | (word & 0xc00003ffU));
    }
    if (PacAut.Matches(word)) {
        return (word & (1U << 13)) && rn != 31 ? Nx2Patch{} : replace(nop);
    }
    if (Xpac.Matches(word)) {
        return replace(nop);
    }
    if ((word & 0xfffff01fU) == 0xd503201fU) {
        const auto hint = (word >> 5) & 127;
        if (hint == 7 || hint == 8 || hint == 10 || hint == 12 || hint == 14 ||
            (hint >= 24 && hint <= 31)) {
            return replace(nop);
        }
    }
    if (Reta.Matches(word)) {
        return replace(0xd65f03c0U);
    }
    if (Bra.Matches(word)) {
        if (rn == 31 || (!(word & (1U << 24)) && rd != 31)) {
            return {};
        }
        return replace(((word & (1U << 21)) ? 0xd63f0000U : 0xd61f0000U) | (rn << 5));
    }
    if (Pacga.Matches(word) || Ldra.Matches(word)) {
        return emulate();
    }
    if (Stg.Matches(word)) {
        const auto op = (word >> 22) & 3;
        const auto mode = (word >> 10) & 3;
        if (mode == 0) {
            return op == 1 ? replace(nop) : Nx2Patch{};
        }
        if (op & 1) {
            return emulate();
        }
        const auto offset = SignExtend((word >> 12) & 511, 9) * 16;
        return replace(mode == 2 ? nop : AddOffset(rn, rn, offset));
    }
    if (Stgp.Matches(word)) {
        return ((word >> 23) & 3) ? emulate() : Nx2Patch{};
    }
    if (Irg.Matches(word)) {
        return replace(AddOffset(rd, rn, 0));
    }
    if (Gmi.Matches(word)) {
        return replace(rd == 31 ? nop : 0xb2400000U | (((word >> 16) & 31) << 5) | rd);
    }
    if (Addg.Matches(word) || Subg.Matches(word)) {
        const auto offset = static_cast<std::int64_t>((word >> 16) & 63) * 16;
        return replace(AddOffset(rd, rn, Subg.Matches(word) ? -offset : offset));
    }
    return {};
}

struct Nx2RegisterState {
    std::span<std::uint64_t, 31> regs;
    std::uint64_t& sp;

    std::uint64_t Read(unsigned r) const { return r == 31 ? 0 : regs[r]; }
    std::uint64_t ReadBase(unsigned r) const { return r == 31 ? sp : regs[r]; }
    void Write(unsigned r, std::uint64_t value) { if (r != 31) regs[r] = value; }
    void WriteBase(unsigned r, std::uint64_t value) { if (r == 31) sp = value; else regs[r] = value; }
};

template <typename Memory>
bool EmulateNx2Instruction(std::uint32_t word, Nx2RegisterState state, Memory& memory) {
    using namespace Nx2Detail;
    const auto rt = word & 31;
    const auto rn = (word >> 5) & 31;
    if (Pacga.Matches(word)) {
        state.Write(rt, (state.Read(rn) ^ state.ReadBase((word >> 16) & 31)) &
                            0xffffffff00000000ULL);
        return true;
    }
    if (Ldra.Matches(word)) {
        const bool writeback = word & (1U << 11);
        if (writeback && rn == rt && rn != 31) {
            return false;
        }
        const auto imm = ((word >> 12) & 511) | ((word >> 13) & 512);
        const auto address = state.ReadBase(rn) + SignExtend(imm, 10) * 8;
        std::uint64_t value;
        if (!memory.ReadBlock(address, &value, sizeof(value))) {
            return false;
        }
        if (writeback) state.WriteBase(rn, address);
        state.Write(rt, value);
        return true;
    }
    if (Stg.Matches(word)) {
        const auto op = (word >> 22) & 3;
        const auto mode = (word >> 10) & 3;
        if (!mode || !(op & 1)) {
            return false;
        }
        const auto base = state.ReadBase(rn);
        const auto offset = SignExtend((word >> 12) & 511, 9) * 16;
        const auto address = mode == 1 ? base : base + offset;
        const std::array<std::uint64_t, 4> zero{};
        const std::size_t bytes = (op & 2) ? 32 : 16;
        if (!memory.WriteBlock(address, zero.data(), bytes)) {
            return false;
        }
        if (mode != 2) state.WriteBase(rn, base + offset);
        return true;
    }
    if (Stgp.Matches(word)) {
        const auto mode = (word >> 23) & 3;
        const auto rt2 = (word >> 10) & 31;
        if (!mode || (mode != 2 && rn != 31 && (rn == rt || rn == rt2))) {
            return false;
        }
        const auto base = state.ReadBase(rn);
        const auto offset = SignExtend((word >> 15) & 127, 7) * 16;
        const auto address = mode == 1 ? base : base + offset;
        const std::array values{state.Read(rt), state.Read(rt2)};
        if (!memory.WriteBlock(address, values.data(), sizeof(values))) {
            return false;
        }
        if (mode != 2) state.WriteBase(rn, base + offset);
        return true;
    }
    return false;
}

} // namespace Core::NCE
