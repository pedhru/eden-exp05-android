#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include "nx2_reference.h"
#include "core/arm/nce/nx2.h"

int main() {
    std::mt19937 random{0x4e583205};
    std::size_t checks{};
    auto check = [&](std::uint32_t word) {
        const auto before = ReferenceNCE::GetNx2Patch(word);
        const auto after = Core::NCE::GetNx2Patch(word);
        assert(static_cast<int>(before.kind) == static_cast<int>(after.kind));
        assert(before.instruction == after.instruction);
        ++checks;
    };
    for (std::uint32_t high = 0; high < 256; ++high) {
        check(high << 24);
        check((high << 24) | 0xffffff);
        for (int i = 0; i < 8192; ++i) check((high << 24) | (random() & 0xffffff));
    }
    using namespace ReferenceNCE::Nx2Detail;
    for (auto pattern : {Ldapr, PacAut, Xpac, Pacga, Reta, Bra, Ldra, Stg, Stgp, Irg, Gmi, Addg, Subg}) {
        for (int i = 0; i < 8192; ++i) check(pattern.bits | (random() & ~pattern.mask));
    }
    for (std::uint32_t hint = 0; hint < 128; ++hint) check(0xd503201f | (hint << 5));
    std::printf("NCE filter: %zu differential checks passed\n", checks);
}
