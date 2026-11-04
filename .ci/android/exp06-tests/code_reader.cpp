#include <array>
#include <cassert>
#include <limits>
#include "core/arm/nce/code_reader.h"
int main() {
    using namespace Core::NCE;
    static_assert(IsValidCodeRange(16,4,12));
    static_assert(!IsValidCodeRange(16,4,16));
    static_assert(!IsValidCodeRange(16,1,4));
    static_assert(!IsValidCodeRange(16,0,3));
    static_assert(!IsValidCodeRange(16,std::numeric_limits<std::size_t>::max(),4));
    std::array<std::uint8_t, 17> storage{};
    std::span<std::uint8_t> bytes{storage.data()+1,16};
    for (std::uint32_t i=0; i<100000; ++i) {
        for (int k=0;k<4;++k) bytes[k]=(i>>(8*k))&255;
        assert(ReadCodeWord(bytes,0)==i);
    }
}
