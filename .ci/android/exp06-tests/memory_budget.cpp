#include <cassert>
#include <cstdint>
#include <limits>
#include "video_core/vulkan_common/android_memory_budget.h"
int main() {
    using Vulkan::AndroidDeviceMemoryBudget;
    constexpr std::uint64_t G = 1ULL << 30;
    static_assert(AndroidDeviceMemoryBudget(3*G, G, 12*G, false) == 7*G/4);
    static_assert(AndroidDeviceMemoryBudget(G, 2*G, 12*G, false) == 0);
    static_assert(AndroidDeviceMemoryBudget(0, 0, 0, false) == 0);
    static_assert(AndroidDeviceMemoryBudget(12*G, 0, 12*G, false) == 4*G);
    static_assert(AndroidDeviceMemoryBudget(12*G, 0, 12*G, true) == 6*G);
    std::uint64_t state = 0x12345678;
    auto random = [&] { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; };
    for (int i=0; i<1000000; ++i) {
        auto b=random(), u=random(), c=random();
        for (bool aggressive : {false, true}) {
            auto result=AndroidDeviceMemoryBudget(b,u,c,aggressive);
            assert(result <= (b > u ? b-u : 0));
            assert(result <= c);
            assert(result <= (aggressive ? 6 : 4)*G);
        }
    }
    assert(AndroidDeviceMemoryBudget(std::numeric_limits<std::uint64_t>::max(),0,
        std::numeric_limits<std::uint64_t>::max(),false)==4*G);
}
