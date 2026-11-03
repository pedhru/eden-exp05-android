// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Loader::Detail {
enum class NsoReadError { None, InvalidSize, OutOfBounds, ShortRead, InvalidRead, DecodeFailed };
struct NsoReadResult {
    NsoReadError error{};
    std::size_t bytes_read{};
    int decoded_size{};
};

constexpr NsoReadError ValidateNsoSegmentRange(std::uint64_t file_size, std::uint64_t offset,
                                             std::size_t output_size, std::size_t packed_size,
                                             bool compressed) {
    const auto stored_size = compressed ? packed_size : output_size;
    if (compressed && (output_size > std::numeric_limits<int>::max() ||
                       packed_size > std::numeric_limits<int>::max() ||
                       (output_size != 0 && packed_size == 0))) {
        return NsoReadError::InvalidSize;
    }
    if (offset > file_size || stored_size > file_size - offset) {
        return NsoReadError::OutOfBounds;
    }
    return NsoReadError::None;
}

template <typename Read, typename Decode>
NsoReadResult ReadNsoSegment(std::uint64_t file_size, std::uint64_t offset,
                            std::size_t packed_size, std::span<std::uint8_t> output,
                            bool compressed, std::vector<std::uint8_t>& scratch, Read read,
                            Decode decode) {
    const auto error = ValidateNsoSegmentRange(file_size, offset, output.size(), packed_size,
                                             compressed);
    if (error != NsoReadError::None) return {error};
    if (output.empty() && (!compressed || packed_size == 0)) return {};
    if (compressed) scratch.resize(packed_size);
    const auto input = compressed ? std::span{scratch} : output;
    std::size_t done{};
    while (done < input.size()) {
        const auto remaining = input.size() - done;
        const auto received = read(input.data() + done, remaining, offset + done);
        if (received == 0) return {NsoReadError::ShortRead, done};
        if (received > remaining) return {NsoReadError::InvalidRead, done};
        done += received;
    }
    if (!compressed) return {NsoReadError::None, done};
    const int decoded = decode(output, std::span<const std::uint8_t>{input});
    if (decoded < 0 || static_cast<std::size_t>(decoded) != output.size()) {
        return {NsoReadError::DecodeFailed, done, decoded};
    }
    return {NsoReadError::None, done, decoded};
}
} // namespace Loader::Detail
