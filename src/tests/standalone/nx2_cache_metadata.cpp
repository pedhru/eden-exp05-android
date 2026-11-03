// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include "core/file_sys/nacp_language.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache_io.h"

#ifdef NX2_TEST_NO_LSAN
extern "C" int __lsan_is_turned_off() {
    return 1;
}
#endif

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace Vulkan::Detail;
std::size_t checks{};

void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(1);
    }
}

void PutU32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = value >> (i * 8);
}

VkPhysicalDeviceProperties Properties() {
    VkPhysicalDeviceProperties properties{};
    properties.vendorID = 0x5143;
    properties.deviceID = 0x650;
    for (unsigned i = 0; i < VK_UUID_SIZE; ++i)
        properties.pipelineCacheUUID[i] = i * 7;
    return properties;
}

Bytes Fixture(const VkPhysicalDeviceProperties& properties) {
    Bytes data(80, 23);
    PutU32(data, 0, 32);
    PutU32(data, 4, VK_PIPELINE_CACHE_HEADER_VERSION_ONE);
    PutU32(data, 8, properties.vendorID);
    PutU32(data, 12, properties.deviceID);
    std::copy_n(properties.pipelineCacheUUID, VK_UUID_SIZE, data.begin() + 16);
    return data;
}

void TestHeaders() {
    const auto properties = Properties();
    auto data = Fixture(properties);
    Check(CompatiblePipelineCache(data, properties), "valid driver cache");
    for (std::size_t size = 0; size < 32; ++size) {
        Check(!CompatiblePipelineCache(std::span{data}.first(size), properties),
              "truncated driver header");
    }
    for (const auto offset : {0, 4, 8, 12, 16, 31}) {
        auto bad = data;
        bad[offset] ^= 1;
        Check(!CompatiblePipelineCache(bad, properties), "header/driver/UUID mismatch");
    }
    data.resize(32);
    Check(CompatiblePipelineCache(data, properties), "empty cache with valid driver header");
    Check(ValidPipelineCacheSize(MaxPipelineCacheBytes), "maximum bounded cache size");
    Check(!ValidPipelineCacheSize(MaxPipelineCacheBytes + 1), "oversized cache rejected");
    Check(!ValidPipelineCacheSize(std::numeric_limits<std::uint64_t>::max()),
          "overflow-sized cache rejected");
}

void TestSnapshots() {
    const auto fixture = Fixture(Properties());
    Bytes output;
    Check(ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  std::copy(fixture.begin(), fixture.end(), static_cast<std::uint8_t*>(data));
                  *size = fixture.size();
                  return VK_SUCCESS;
              },
              output) &&
              output == fixture,
          "complete snapshot");
    unsigned calls{};
    Check(ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  ++calls;
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  if (calls == 2)
                      return VK_INCOMPLETE;
                  std::copy(fixture.begin(), fixture.end(), static_cast<std::uint8_t*>(data));
                  *size = fixture.size();
                  return VK_SUCCESS;
              },
              output) &&
              output == fixture && calls == 4,
          "retry cache growth");
    calls = 0;
    Check(!ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  ++calls;
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  return VK_INCOMPLETE;
              },
              output) &&
              output.empty() && calls == 6,
          "bounded retries for growing cache");
    for (const auto size : {std::size_t{0}, std::size_t{31}, MaxPipelineCacheBytes + 1,
                            std::numeric_limits<std::size_t>::max()}) {
        Check(!ReadPipelineCacheSnapshot(
                  [&](std::size_t* count, void* data) {
                      Check(data == nullptr, "reject size before allocating/reading");
                      *count = size;
                      return VK_SUCCESS;
                  },
                  output) &&
                  output.empty(),
              "invalid queried cache size");
    }
    Check(!ReadPipelineCacheSnapshot(
              [](std::size_t*, void*) { return VK_ERROR_OUT_OF_HOST_MEMORY; }, output) &&
              output.empty(),
          "query failure");
    Check(!ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  return VK_ERROR_UNKNOWN;
              },
              output) &&
              output.empty(),
          "read failure");
    Check(!ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  *size += 1;
                  return VK_SUCCESS;
              },
              output) &&
              output.empty(),
          "invalid written count");
    Check(ReadPipelineCacheSnapshot(
              [&](std::size_t* size, void* data) {
                  if (!data) {
                      *size = fixture.size();
                      return VK_SUCCESS;
                  }
                  std::copy_n(fixture.begin(), 32, static_cast<std::uint8_t*>(data));
                  *size = 32;
                  return VK_SUCCESS;
              },
              output) &&
              output.size() == 32,
          "cache shrinks between query and read");
}

Bytes ReadBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>{file}, {});
}

void TestFiles(const std::filesystem::path& directory) {
    constexpr std::uint32_t version = 18;
    const auto properties = Properties();
    auto data = Fixture(properties);
    const auto path = directory / "cache.bin";
    Check(ReadPipelineCacheFile(path, version, properties).status ==
              PipelineCacheFileStatus::Missing,
          "missing cache starts empty");
    Check(WritePipelineCacheFile(path, version, data), "write real cache file");
    const auto original = ReadBytes(path);
    auto loaded = ReadPipelineCacheFile(path, version, properties);
    Check(loaded.status == PipelineCacheFileStatus::Ready && loaded.data == data,
          "read real cache file");
    Check(ReadPipelineCacheFile(path, version + 1, properties).status ==
              PipelineCacheFileStatus::Invalid,
          "cache format version mismatch");
    auto other_driver = properties;
    other_driver.pipelineCacheUUID[4] ^= 1;
    Check(ReadPipelineCacheFile(path, version, other_driver).status ==
              PipelineCacheFileStatus::Incompatible,
          "other driver is rejected");
    Check(ReadBytes(path) == original, "incompatible cache is preserved for its driver");
    auto temporary = path;
    temporary += ".tmp";
    std::filesystem::create_directory(temporary);
    Check(!WritePipelineCacheFile(path, version, data), "failed temporary write");
    Check(ReadBytes(path) == original, "failed write preserves previous cache");
    std::filesystem::remove(temporary);
    data.back() ^= 1;
    Check(WritePipelineCacheFile(path, version, data), "replace existing cache");
    Check(ReadPipelineCacheFile(path, version, properties).data == data,
          "updated data is complete");
    Check(!std::filesystem::exists(temporary), "temporary renamed after successful write");
    Check(!WritePipelineCacheFile(path, version, {}), "empty snapshot never overwrites cache");
    Check(ReadPipelineCacheFile(path, version, properties).data == data,
          "empty snapshot preserves cache");
    const auto blocked = directory / "nonempty-directory";
    std::filesystem::create_directory(blocked);
    {
        std::ofstream marker(blocked / "keep");
        marker << "previous";
    }
    Check(!WritePipelineCacheFile(blocked, version, data), "failed rename");
    Check(std::filesystem::exists(blocked / "keep"), "failed rename preserves target");
    Check(!std::filesystem::exists(blocked.string() + ".tmp"), "failed rename removes temporary");
    for (const auto size :
         {std::uintmax_t{0}, std::uintmax_t{11}, std::uintmax_t{43},
          std::uintmax_t{MaxPipelineCacheBytes} + PipelineCacheFileHeaderSize + 1}) {
        std::filesystem::resize_file(path, size);
        Check(ReadPipelineCacheFile(path, version, properties).status ==
                  PipelineCacheFileStatus::Invalid,
              "bounded file size checked before allocation");
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(original.data()), original.size());
    }
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.put('X');
    }
    Check(ReadPipelineCacheFile(path, version, properties).status ==
              PipelineCacheFileStatus::Invalid,
          "corrupt outer header");
}

Bytes Compress(std::span<const std::uint8_t> data, int window_bits = -MAX_WBITS) {
    z_stream stream{};
    Check(deflateInit2(&stream, 6, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) == Z_OK,
          "create compressed language fixture");
    Bytes packed(compressBound(data.size()));
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = data.size();
    stream.next_out = packed.data();
    stream.avail_out = packed.size();
    Check(deflate(&stream, Z_FINISH) == Z_STREAM_END, "finish compressed fixture");
    packed.resize(stream.total_out);
    deflateEnd(&stream);
    return packed;
}

void TestTitles() {
    using namespace FileSys::Detail;
    for (const auto count : {16, 18, 24, 32}) {
        Bytes original(count * NacpLanguageEntrySize);
        for (std::size_t i = 0; i < original.size(); ++i)
            original[i] = (i * 7) ^ (i >> 8);
        const auto packed = Compress(original);
        Check(DecompressNacpTitles(packed, packed.size()) == original,
              "complete language entries incl. pt-BR");
        for (std::size_t cut = 1; cut <= std::min<std::size_t>(packed.size(), 16); ++cut) {
            Check(DecompressNacpTitles(std::span{packed}.first(packed.size() - cut),
                                       packed.size() - cut)
                      .empty(),
                  "truncated deflate stream");
        }
    }
    for (const auto size :
         {std::size_t{0}, std::size_t{1}, NacpLanguageEntrySize - 1, NacpLanguageEntrySize + 1,
          MaxNacpLanguageBytes - 1, MaxNacpLanguageBytes + 1,
          MaxNacpLanguageBytes + NacpLanguageEntrySize}) {
        const auto packed = Compress(Bytes(size, 77));
        Check(DecompressNacpTitles(packed, packed.size()).empty(),
              "incomplete or oversized language records");
    }
    const Bytes corrupt(64, 0xff);
    Check(DecompressNacpTitles(corrupt, corrupt.size()).empty(), "corrupt deflate data");
    const auto packed = Compress(Bytes(NacpLanguageEntrySize, 42));
    Check(DecompressNacpTitles(packed, packed.size() + 1).empty(),
          "declared payload exceeds supplied bytes");
    Check(DecompressNacpTitles(packed, 65535).empty(), "oversized stored length");
    Check(DecompressNacpTitles(Bytes(0x3000), 0x2fff).empty(), "NACP buffer capacity");
    Check(DecompressNacpTitles({}, 0).empty(), "empty payload");
    const auto wrapped = Compress(Bytes(NacpLanguageEntrySize), MAX_WBITS);
    Check(DecompressNacpTitles(wrapped, wrapped.size()).empty(),
          "reject wrong compression envelope");
}
} // namespace

int main(int argc, char** argv) {
    Check(argc == 2, "temporary directory supplied");
    TestHeaders();
    TestSnapshots();
    TestFiles(argv[1]);
    TestTitles();
    std::cout << "{\"checks\":" << checks << ",\"zlib\":\"" << zlibVersion()
              << "\",\"result\":\"passed\"}\n";
}
