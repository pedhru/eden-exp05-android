// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <zlib.h>

namespace FileSys::Detail {

inline constexpr std::size_t NacpLanguageEntrySize = 0x300;
inline constexpr std::size_t MaxNacpLanguageBytes = NacpLanguageEntrySize * 32;

inline std::vector<std::uint8_t> DecompressNacpTitles(std::span<const std::uint8_t> buffer,
                                                      std::size_t compressed_size) {
    if (compressed_size == 0 || compressed_size > buffer.size() || compressed_size > 0x2FFE) {
        return {};
    }
    // The extra byte distinguishes a complete maximum-sized table from an oversized table.
    std::vector<std::uint8_t> output(MaxNacpLanguageBytes + 1);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(buffer.data());
    stream.avail_in = static_cast<uInt>(compressed_size);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return {};
    }
    const int result = inflate(&stream, Z_FINISH);
    const auto size = stream.total_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END || size == 0 || size > MaxNacpLanguageBytes ||
        size % NacpLanguageEntrySize != 0) {
        return {};
    }
    output.resize(size);
    return output;
}

} // namespace FileSys::Detail
