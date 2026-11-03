// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Core::Detail {
class FrameCounter {
public:
    void Add() { frames.fetch_add(1, std::memory_order_relaxed); }
    std::uint64_t Take() { return frames.exchange(0, std::memory_order_relaxed); }
private:
    std::atomic<std::uint64_t> frames{};
};
struct FrameTimeSummary {
    std::size_t samples{};
    double median_ms{};
    double p95_ms{};
    double p99_ms{};
    double maximum_ms{};
};

// Access is serialized by PerfStats. Frame submission never allocates memory here.
class FrameTimeWindow {
public:
    void Add(double milliseconds) {
        if (!std::isfinite(milliseconds) || milliseconds <= 0) return;
        values[index] = milliseconds;
        index = (index + 1) % values.size();
        count = std::min(count + 1, values.size());
    }
    FrameTimeSummary Summarize() const {
        if (count == 0) return {};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.begin() + count);
        const auto percentile = [&](std::size_t percent) {
            return sorted[(count * percent + 99) / 100 - 1];
        };
        return {count, percentile(50), percentile(95), percentile(99), sorted[count - 1]};
    }
private:
    std::array<double, 512> values{};
    std::size_t index{};
    std::size_t count{};
};
} // namespace Core::Detail
