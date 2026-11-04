#!/usr/bin/env python3
"""Compile-test the actual sparse texture helpers against a minimal CPU-only harness.

No Vulkan, SDL, Android SDK, or GPU is needed. The simulated GOB decoder validates
rectangular copies/bands and API contracts, not the device-specific swizzle layout.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/video_core/texture_cache/util.cpp').read_text()
start = source.index('namespace {\n\n// A block-linear mip is stored')
end = source.index('void ConvertedLayout(', start)
fragment = source[start:end]
header = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
using GPUVAddr = u64;
#define LOG_WARNING(...) do {} while(0)
namespace Common {
template<typename T> T DivCeil(T value, T step) { return (value + step - 1) / step; }
}
namespace Tegra {
struct MemoryManager {
    std::vector<u8> data;
    bool UsesNativeTiling() const { return false; }
    void ReadBlockUnsafe(u64 addr, void* dest, size_t size) const {
        if (addr > data.size() || size > data.size() - addr) throw std::runtime_error("out-of-bounds guest read");
        std::memcpy(dest, data.data() + addr, size);
    }
};
namespace Texture {
void UnswizzleTexture(std::span<u8> out, std::span<const u8> in, u32 bpp,
                      u32 width, u32 height, u32 depth, u32 block_height,
                      u32 block_depth, u32 alignment, bool native) {
    assert(block_depth == 0 && !native && depth == 1);
    assert((width * bpp) % 64 == 0 && in.size() >= (width * bpp / 64) * (512u << block_height));
    (void)alignment;
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            size_t xbytes = size_t{x} * bpp;
            size_t addr = (xbytes / 64) * (512u << block_height) +
                          size_t{y / 8} * 512 + size_t{y % 8} * 64 + xbytes % 64;
            std::memcpy(out.data() + (size_t{y} * width + x) * bpp, in.data() + addr, bpp);
        }
    }
}
}}
namespace VideoCommon {
constexpr u32 GOB_SIZE = 512;
constexpr u32 GOB_SIZE_X = 64;
constexpr u32 GOB_SIZE_Y = 8;
constexpr u32 GOB_SIZE_X_SHIFT = 6;
constexpr u32 MAX_MIP_LEVELS = 16;
struct Extent3D { u32 width, height, depth; };
struct SubresourceExtent { s32 levels = 1, layers = 1; };
enum class ImageType { e2D };
struct ImageInfo {
    ImageType type = ImageType::e2D;
    Extent3D size{256, 128, 1};
    Extent3D block{0, 0, 0};
    SubresourceExtent resources{};
    u32 num_samples = 1;
    u32 format = 0;
};
struct LevelRect { s32 level{}; u32 x{}, y{}, width{}, height{}; };
struct BufferImageCopy {
    size_t buffer_offset, buffer_size;
    u32 buffer_row_length, buffer_image_height;
    struct { s32 base_level, base_layer, num_layers; } image_subresource;
    struct { s32 x, y, z; } image_offset;
    Extent3D image_extent;
};
using LevelArray = std::array<u32, MAX_MIP_LEVELS>;
u32 BytesPerBlock(u32 format) { return format == 4 ? 4u : 1u; }
u32 DefaultBlockWidth(u32) { return 1; }
u32 DefaultBlockHeight(u32) { return 1; }
Extent3D DefaultBlockSize(u32) { return {1,1,1}; }
Extent3D AdjustMipSize(Extent3D size, s32 level) {
    return {(std::max)(size.width >> level, 1u), (std::max)(size.height >> level, 1u), 1};
}
Extent3D AdjustTileSize(Extent3D size, Extent3D) { return size; }
Extent3D AdjustMipBlockSize(Extent3D, Extent3D block, u32 level, u32) { return level == 0 ? block : Extent3D{0,0,0}; }
LevelArray CalculateMipLevelSizes(const ImageInfo& info) {
    LevelArray sizes{};
    for(s32 i=0; i<info.resources.levels; ++i) {
        auto mip=AdjustMipSize(info.size,i);
        u32 bh=(i == 0 ? info.block.height : 0);
        u32 bpp=BytesPerBlock(info.format);
        sizes[i]=Common::DivCeil(mip.width * bpp,64u)*
                 Common::DivCeil(mip.height,8u << bh)*(512u << bh);
    }
    return sizes;
}
LevelArray CalculateMipLevelOffsets(const ImageInfo& info) {
    LevelArray offsets{};
    auto sizes=CalculateMipLevelSizes(info);
    for(s32 i=1;i<info.resources.levels;++i) offsets[i]=offsets[i-1]+sizes[i-1];
    return offsets;
}
u32 CalculateGuestSizeInBytes(const ImageInfo& info) {
    auto sizes=CalculateMipLevelSizes(info);
    u32 bytes=0;
    for(s32 i=0;i<info.resources.levels;++i) bytes+=sizes[i];
    return bytes;
}
'''
footer = r'''
}
int main() {
    using namespace VideoCommon;
    ImageInfo info;
    std::vector<std::pair<u64,u64>> one{{0,512}};
    auto rects = SparseTileRects(info, one, 64, 64, 16);
    assert(rects.size()==1 && rects[0].level==0 && rects[0].x==0 && rects[0].y==0);
    assert(rects[0].width==64 && rects[0].height==64);
    assert(UnswizzledRectsBytes(info,rects)==4096);
    Tegra::MemoryManager mem;
    mem.data.resize(CalculateGuestSizeInBytes(info));
    for(size_t i=0;i<mem.data.size();++i) mem.data[i]=static_cast<u8>(i/64);
    std::vector<u8> output(UnswizzledRectsBytes(info,rects));
    auto copies=UnswizzleImageRects(mem,0,info,rects,output);
    assert(copies.size()==1 && copies[0].buffer_size==4096);
    assert(output[0]==mem.data[0]);
    assert(output[64]==mem.data[64]);
    assert(output[64*8]==mem.data[2048]); // next band of 8 guest rows
    // Adjacent host tiles become one rectangle, not one upload each.
    std::vector<std::pair<u64,u64>> two{{0,1024}};
    auto joined=SparseTileRects(info,two,64,64,16);
    assert(joined.size()==1 && joined[0].width==128);
    // A guest GOB in the second half of the image selects the next tile row.
    std::vector<std::pair<u64,u64>> lower{{16384,512}};
    auto bottom=SparseTileRects(info,lower,64,64,16);
    assert(bottom.size()==1 && bottom[0].y==64);
    // Mip tail detection selects a full mip, even when a small byte range changes.
    info.resources.levels=2;
    std::vector<std::pair<u64,u64>> mip{{32768,512}};
    auto tail=SparseTileRects(info,mip,64,64,1);
    assert(tail.size()==1 && tail[0].level==1 && tail[0].width==128 && tail[0].height==64);
    mem.data.resize(CalculateGuestSizeInBytes(info));
    output.resize(UnswizzledRectsBytes(info,tail));
    assert(UnswizzleImageRects(mem,0,info,tail,output).size()==1);
    // Sparse tiles may start inside a GOB, and the staging copy must crop rows/columns.
    info.resources.levels=1;
    info.size={128,64,1};
    info.format=4;
    info.block={0,1,0};
    std::vector<std::pair<u64,u64>> small{{0,1024}};
    auto bounded=SparseTileRects(info,small,10,12,16);
    assert(bounded.size()==2 && bounded[0].x==0 && bounded[0].width==20 &&
           bounded[0].height==12 && bounded[1].y==12);
    mem.data.resize(CalculateGuestSizeInBytes(info));
    for(size_t i=0;i<mem.data.size();++i) mem.data[i]=static_cast<u8>(i/37);
    output.resize(UnswizzledRectsBytes(info,bounded));
    copies=UnswizzleImageRects(mem,0,info,bounded,output);
    assert(copies.size()==2 && copies[0].buffer_size==20*12*4 &&
           copies[1].buffer_offset==20*12*4);
    assert(std::memcmp(output.data(),mem.data.data(),4)==0);
    assert(std::memcmp(output.data()+20*4,mem.data.data()+64,4)==0);
    // Explicit partially aligned rectangles decode without touching other tiles.
    std::vector<LevelRect> crop{{0,11,7,13,17}};
    output.resize(UnswizzledRectsBytes(info,crop));
    copies=UnswizzleImageRects(mem,0,info,crop,output);
    assert(copies.size()==1 && copies[0].image_offset.x==11 &&
           copies[0].image_offset.y==7);
    // Bad input and overflow must not produce a buffer write.
    std::vector<LevelRect> bad{{0,256,0,64,64}};
    assert(UnswizzleImageRects(mem,0,info,bad,output).empty());
}
'''
compiler = shutil.which('g++') or shutil.which('clang++')
if compiler is None:
    raise SystemExit('C++20 compiler required for sparse texture smoke test')
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'sparse_rects.cpp'
    binary=Path(tmp)/'test_sparse_rects'
    cpp.write_text(header+fragment+footer)
    subprocess.run([compiler,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-o',str(binary),str(cpp)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: sparse tiles, mip tail, GOB bands, partial copies, invalid rectangles (C++20)')
