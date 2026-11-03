// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <system_error>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace Vulkan::Detail {

inline constexpr std::array<std::uint8_t, 8> PipelineCacheMagic{'y', 'u', 'z', 'u',
                                                                'v', 'k', 'c', 'h'};
inline constexpr std::size_t PipelineCacheFileHeaderSize = 12;
inline constexpr std::size_t PipelineCacheDriverHeaderSize = 32;
inline constexpr std::size_t MaxPipelineCacheBytes = 256ULL << 20;

constexpr bool ValidPipelineCacheSize(std::uint64_t size) {
    return size >= PipelineCacheDriverHeaderSize && size <= MaxPipelineCacheBytes;
}

inline std::uint32_t ReadCacheU32(const std::uint8_t* data) {
    return std::uint32_t{data[0]} | (std::uint32_t{data[1]} << 8) | (std::uint32_t{data[2]} << 16) |
           (std::uint32_t{data[3]} << 24);
}

inline bool CompatiblePipelineCache(std::span<const std::uint8_t> data,
                                    const VkPhysicalDeviceProperties& properties) {
    // Vulkan's cache header is tightly packed and little endian, even on big endian hosts.
    return ValidPipelineCacheSize(data.size()) &&
           ReadCacheU32(data.data()) == PipelineCacheDriverHeaderSize &&
           ReadCacheU32(data.data() + 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
           ReadCacheU32(data.data() + 8) == properties.vendorID &&
           ReadCacheU32(data.data() + 12) == properties.deviceID &&
           std::equal(std::begin(properties.pipelineCacheUUID),
                      std::end(properties.pipelineCacheUUID), data.begin() + 16);
}

template <typename Read>
bool ReadPipelineCacheSnapshot(Read read, std::vector<std::uint8_t>& data) {
    data.clear();
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        std::size_t size{};
        if (read(&size, nullptr) != VK_SUCCESS || !ValidPipelineCacheSize(size)) {
            break;
        }
        data.resize(size);
        std::size_t written = data.size();
        const auto result = read(&written, data.data());
        if (written > data.size()) {
            break;
        }
        if (result == VK_SUCCESS && ValidPipelineCacheSize(written)) {
            data.resize(written);
            return true;
        }
        if (result != VK_INCOMPLETE) {
            break;
        }
    }
    data.clear();
    return false;
}

enum class PipelineCacheFileStatus { Missing, Invalid, Incompatible, Ready };

struct PipelineCacheFile {
    PipelineCacheFileStatus status{};
    std::vector<std::uint8_t> data;
};

inline PipelineCacheFile ReadPipelineCacheFile(const std::filesystem::path& filename,
                                               std::uint32_t version,
                                               const VkPhysicalDeviceProperties& properties) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {PipelineCacheFileStatus::Missing, {}};
    }
    const auto end = file.tellg();
    if (end < static_cast<std::streamoff>(PipelineCacheFileHeaderSize) ||
        !ValidPipelineCacheSize(static_cast<std::uint64_t>(end) - PipelineCacheFileHeaderSize)) {
        return {PipelineCacheFileStatus::Invalid, {}};
    }
    file.seekg(0, std::ios::beg);
    std::array<std::uint8_t, PipelineCacheFileHeaderSize> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!file ||
        !std::equal(PipelineCacheMagic.begin(), PipelineCacheMagic.end(), header.begin()) ||
        ReadCacheU32(header.data() + PipelineCacheMagic.size()) != version) {
        return {PipelineCacheFileStatus::Invalid, {}};
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end) - header.size());
    file.read(reinterpret_cast<char*>(data.data()), data.size());
    if (!file) {
        return {PipelineCacheFileStatus::Invalid, {}};
    }
    if (!CompatiblePipelineCache(data, properties)) {
        return {PipelineCacheFileStatus::Incompatible, {}};
    }
    return {PipelineCacheFileStatus::Ready, std::move(data)};
}

inline bool WritePipelineCacheFile(const std::filesystem::path& filename, std::uint32_t version,
                                   std::span<const std::uint8_t> data) {
    if (!ValidPipelineCacheSize(data.size())) {
        return false;
    }
    auto temporary = filename;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    const std::array<std::uint8_t, 4> encoded_version{
        static_cast<std::uint8_t>(version), static_cast<std::uint8_t>(version >> 8),
        static_cast<std::uint8_t>(version >> 16), static_cast<std::uint8_t>(version >> 24)};
    file.write(reinterpret_cast<const char*>(PipelineCacheMagic.data()), PipelineCacheMagic.size());
    file.write(reinterpret_cast<const char*>(encoded_version.data()), encoded_version.size());
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    std::error_code error;
    if (file) {
        std::filesystem::rename(temporary, filename, error);
        if (!error) {
            return true;
        }
    }
    std::filesystem::remove(temporary, error);
    return false;
}

} // namespace Vulkan::Detail
