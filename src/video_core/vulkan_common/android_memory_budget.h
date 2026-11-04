// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace Vulkan {
// Android shares physical memory with the CPU. Do not promote heap capacity to free budget.
constexpr std::uint64_t AndroidDeviceMemoryBudget(std::uint64_t budget,
                                                  std::uint64_t usage,
                                                  std::uint64_t local_capacity,
                                                  bool aggressive) {
    constexpr std::uint64_t GiB = 1ULL << 30;
    const auto available = budget > usage ? budget - usage : 0;
    const auto usable = (std::min)(available, local_capacity);
    const auto reserve = (std::min)(usable / 8, GiB);
    return (std::min)(usable - reserve, (aggressive ? 6 : 4) * GiB);
}
} // namespace Vulkan
