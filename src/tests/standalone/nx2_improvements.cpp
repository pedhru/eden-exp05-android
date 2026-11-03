// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include "core/frame_time_window.h"
#include "core/loader/nso_segment_reader.h"
#include "video_core/cache_memory_budget.h"

extern "C" int LZ4_compressBound(int);
extern "C" int LZ4_compress_default(const char*, char*, int, int);
extern "C" int LZ4_decompress_safe(const char*, char*, int, int);
extern "C" const char* LZ4_versionString();
#ifdef NX2_TEST_NO_LSAN
extern "C" int __lsan_is_turned_off() { return 1; }
#endif
namespace {
using Bytes = std::vector<std::uint8_t>;
using Loader::Detail::NsoReadError;
std::size_t checks{};
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAILED: " << description << '\n'; std::exit(1); }
}
int Decode(std::span<std::uint8_t> output, std::span<const std::uint8_t> input) {
    return LZ4_decompress_safe(reinterpret_cast<const char*>(input.data()),
        reinterpret_cast<char*>(output.data()), static_cast<int>(input.size()), static_cast<int>(output.size()));
}
void TestReads() {
    Bytes original(1 << 20);
    for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<std::uint8_t>((i * 31) ^ (i >> 5));
    Bytes packed(LZ4_compressBound(static_cast<int>(original.size())));
    const int size = LZ4_compress_default(reinterpret_cast<const char*>(original.data()),
        reinterpret_cast<char*>(packed.data()), static_cast<int>(original.size()), static_cast<int>(packed.size()));
    Check(size > 0, "compress fixture"); packed.resize(size);
    Bytes output(original.size()), scratch;
    for (const std::size_t fragment : {std::size_t{7}, packed.size()}) {
        auto result = Loader::Detail::ReadNsoSegment(packed.size(), 0, packed.size(), output, true, scratch,
            [&](std::uint8_t* dest, std::size_t count, std::uint64_t offset) {
                const auto n = std::min(count, fragment); std::copy_n(packed.data() + offset, n, dest); return n;
            }, Decode);
        Check(result.error == NsoReadError::None && output == original, "complete and fragmented decode");
        Check(result.bytes_read == packed.size(), "all packed bytes read");
    }
    auto read = [&](std::uint8_t* dest, std::size_t count, std::uint64_t offset) {
        std::copy_n(original.data() + offset, count, dest); return count;
    };
    auto result = Loader::Detail::ReadNsoSegment(original.size(), 0, 0, output, false, scratch, read, Decode);
    Check(result.error == NsoReadError::None && output == original, "raw reads use raw size");
    auto never = [](std::uint8_t*, std::size_t, std::uint64_t) -> std::size_t { Check(false, "invalid range must not read"); return 0; };
    result = Loader::Detail::ReadNsoSegment(29404104, 28645443, 759641, output, true, scratch, never, Decode);
    Check(result.error == NsoReadError::OutOfBounds, "980-byte update shortfall");
    result = Loader::Detail::ReadNsoSegment(28684035, 27937157, 747867, output, true, scratch, never, Decode);
    Check(result.error == NsoReadError::OutOfBounds, "989-byte base shortfall");
    result = Loader::Detail::ReadNsoSegment(10, std::numeric_limits<std::uint64_t>::max(), 5, output, true, scratch, never, Decode);
    Check(result.error == NsoReadError::OutOfBounds, "offset overflow");
    Check(Loader::Detail::ValidateNsoSegmentRange(100, 0, std::size_t{1} << 31, 10, true) == NsoReadError::InvalidSize, "decoder integer limit");
    result = Loader::Detail::ReadNsoSegment(1, 0, 1, std::span<std::uint8_t>{}, true, scratch,
        [](std::uint8_t* dest, std::size_t, std::uint64_t) -> std::size_t { *dest = 0; return 1; }, Decode);
    Check(result.error == NsoReadError::None && result.decoded_size == 0, "valid empty LZ4 block");
    result = Loader::Detail::ReadNsoSegment(10, 0, 0, output, true, scratch, never, Decode);
    Check(result.error == NsoReadError::InvalidSize, "nonempty decoded data needs packed input");
    result = Loader::Detail::ReadNsoSegment(1, 0, 1, std::span<std::uint8_t>{}, true, scratch,
        [](std::uint8_t* dest, std::size_t, std::uint64_t) -> std::size_t { *dest = 255; return 1; }, Decode);
    Check(result.error == NsoReadError::DecodeFailed, "invalid empty LZ4 block");
    result = Loader::Detail::ReadNsoSegment(0, 0, 0, std::span<std::uint8_t>{}, true, scratch, never, Decode);
    Check(result.error == NsoReadError::None, "empty segment");
    result = Loader::Detail::ReadNsoSegment(original.size(), 0, 10, output, true, scratch,
        [](std::uint8_t*, std::size_t, std::uint64_t) -> std::size_t { return 0; }, Decode);
    Check(result.error == NsoReadError::ShortRead, "zero progress");
    result = Loader::Detail::ReadNsoSegment(original.size(), 0, 10, output, true, scratch,
        [](std::uint8_t*, std::size_t count, std::uint64_t) { return count + 1; }, Decode);
    Check(result.error == NsoReadError::InvalidRead, "excessive read count");
    for (const int decoded : {-1, 1, static_cast<int>(output.size()) - 1}) {
        result = Loader::Detail::ReadNsoSegment(original.size(), 0, 10, output, true, scratch, read,
            [&](auto, auto) { return decoded; });
        Check(result.error == NsoReadError::DecodeFailed, "failed or short decode");
    }
    Bytes corrupt(64, 255);
    result = Loader::Detail::ReadNsoSegment(corrupt.size(), 0, corrupt.size(), output, true, scratch,
        [&](std::uint8_t* dest, std::size_t count, std::uint64_t) { std::copy_n(corrupt.data(), count, dest); return count; }, Decode);
    Check(result.error == NsoReadError::DecodeFailed, "actual decoder rejects corrupt block");
}
void TestBudgets() {
    using namespace VideoCommon::Detail;
    for (std::uint64_t budget : {std::uint64_t{1}, std::uint64_t{512} << 20, std::uint64_t{2} << 30,
             std::uint64_t{4} << 30, std::uint64_t{12} << 30, std::numeric_limits<std::uint64_t>::max()}) {
        const auto texture = TextureMemoryThresholds(budget), buffer = BufferMemoryThresholds(budget);
        Check(texture.expected <= texture.critical && texture.critical <= budget, "bounded texture thresholds");
        Check(buffer.expected <= buffer.critical && buffer.critical <= budget, "bounded buffer thresholds");
    }
    const auto small = TextureMemoryThresholds(512ULL << 20);
    Check(small.expected == (384ULL << 20) && small.critical == (448ULL << 20), "budget below 2 GiB");
    const auto buffers = BufferMemoryThresholds(2ULL << 30);
    Check(buffers.expected == (512ULL << 20) && buffers.critical == (1ULL << 30), "constrained buffer budget");
    Check(BufferMemoryThresholds(0).expected == (1ULL << 30), "no budget fallback");
    Check(SubtractMemoryReserve(128ULL << 20, 1280ULL << 20) == 0, "no unsigned reserve underflow");
}
void TestFrames() {
    Core::Detail::FrameTimeWindow times;
    Check(times.Summarize().samples == 0, "empty window");
    for (int i = 1; i <= 100; ++i) times.Add(i);
    auto stats = times.Summarize();
    Check(stats.samples == 100 && stats.median_ms == 50 && stats.p95_ms == 95 && stats.p99_ms == 99 && stats.maximum_ms == 100, "known percentiles");
    for (double v : {-1., 0., std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) times.Add(v);
    Check(times.Summarize().samples == 100, "invalid samples ignored");
    for (int i = 101; i <= 600; ++i) times.Add(i);
    stats = times.Summarize();
    Check(stats.samples == 512 && stats.median_ms == 344 && stats.p95_ms == 575 && stats.p99_ms == 595 && stats.maximum_ms == 600, "rolling window");
    Core::Detail::FrameCounter counter;
    std::atomic<int> running{4}; std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back([&] { for (int i = 0; i < 250000; ++i) counter.Add(); running.fetch_sub(1, std::memory_order_release); });
    std::uint64_t received{};
    while (running.load(std::memory_order_acquire)) { received += counter.Take(); std::this_thread::yield(); }
    for (auto& t : threads) t.join();
    received += counter.Take();
    Check(received == 1000000, "no concurrent frame increments lost");
    Check(counter.Take() == 0, "drain resets count");
}
}
int main() {
    TestReads(); TestBudgets(); TestFrames();
    std::cout << "{\"checks\":" << checks << ",\"concurrent_frames\":1000000,\"lz4\":\""
              << LZ4_versionString() << "\",\"result\":\"passed\"}\n";
}
