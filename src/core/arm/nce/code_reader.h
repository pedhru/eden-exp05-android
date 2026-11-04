// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Core::NCE {
constexpr bool IsValidCodeRange(std::size_t image_size, std::size_t offset, std::size_t size) {
    return offset <= image_size && size <= image_size - offset &&
           offset % sizeof(std::uint32_t) == 0 && size % sizeof(std::uint32_t) == 0;
}
// Caller supplies a complete instruction; memcpy also supports unaligned byte storage.
inline std::uint32_t ReadCodeWord(std::span<const std::uint8_t> text, std::size_t index) {
    std::uint32_t word;
    std::memcpy(&word, text.data() + index * sizeof(word), sizeof(word));
    return word;
}
} // namespace Core::NCE
