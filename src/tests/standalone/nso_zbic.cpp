// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif
#include "common/zbic_compression.cpp"
#include "core/loader/nso_segment_reader.h"
#ifdef NX2_TEST_NO_LSAN
extern "C" int __lsan_is_turned_off() { return 1; }
#endif
int main() {
    size_t checks{};
    const auto check = [&](bool v) { assert(v); ++checks; };
    const auto run = [&](std::vector<u8> packed, size_t size) {
        std::vector<u8> output(size), scratch;
        const auto result = Loader::Detail::ReadNsoSegment(packed.size(), 0, packed.size(), output, true, scratch,
            [&](u8* dest, size_t count, u64 offset) { std::copy_n(packed.data() + offset, count, dest); return count; },
            Common::Compression::DecompressDataZBIC);
        return std::pair{result, output};
    };
    const auto compress = [&](std::span<const u8> bytes) {
        std::vector<u8> packed(ZSTD_compressBound(bytes.size()));
        const auto size = ZSTD_compress(packed.data(), packed.size(), bytes.data(), bytes.size(), 3);
        check(!ZSTD_isError(size)); packed.resize(size); check(Common::Compression::IsZBIC(packed)); return packed;
    };
    std::vector<u8> bytes(65536);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<u8>(i * 31);
    auto packed = compress(bytes);
    auto [result, output] = run(packed, bytes.size());
    check(result.error == Loader::Detail::NsoReadError::None && output == bytes);
    const auto short_output = run(packed, bytes.size() + 1).first;
    check(short_output.error == Loader::Detail::NsoReadError::DecodeFailed && short_output.decoded_size == static_cast<int>(bytes.size()));
    packed.pop_back(); check(run(packed, bytes.size()).first.error == Loader::Detail::NsoReadError::DecodeFailed);
    const auto empty = run(compress({}), 0).first;
    check(empty.error == Loader::Detail::NsoReadError::None && empty.decoded_size == 0);
    std::printf("ZBIC NSO reader: %zu checks passed (ASan + UBSan)\n", checks);
}
