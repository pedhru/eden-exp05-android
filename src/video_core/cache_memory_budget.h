// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace VideoCommon::Detail {
struct CacheMemoryThresholds {
    std::uint64_t expected;
    std::uint64_t critical;
};
constexpr CacheMemoryThresholds TextureMemoryThresholds(std::uint64_t budget) {
    const auto margin = std::min<std::uint64_t>(budget / 8, 1ULL << 30);
    return {budget - 2 * margin, budget - margin};
}
constexpr CacheMemoryThresholds BufferMemoryThresholds(std::uint64_t budget) {
    if (budget == 0) return {1ULL << 30, 2ULL << 30};
    return {std::max<std::uint64_t>(1, std::min<std::uint64_t>(budget / 4, 1ULL << 30)),
            std::max<std::uint64_t>(1, std::min<std::uint64_t>(budget / 2, 2ULL << 30))};
}
constexpr std::uint64_t SubtractMemoryReserve(std::uint64_t memory, std::uint64_t reserve) {
    return memory > reserve ? memory - reserve : 0;
}
} // namespace VideoCommon::Detail
