#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>
#include <thread>
#include <cstdio>
#include "shader_recompiler/object_pool.h"
thread_local bool reject_allocations = false;
void* operator new(std::size_t n) {
    if (reject_allocations) throw std::bad_alloc{};
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
struct Item {
    inline static std::atomic<int> alive{};
    Item() { ++alive; }
    ~Item() { --alive; }
};
void Run() {
    Shader::ObjectPool<Item> pool{4};
    auto* root = pool.Create();
    pool.ReleaseExtraChunks();
    for (int count : {0, 1, 4, 5, 17, 1000, 4}) {
        for (int i = 0; i < count; ++i) (void)pool.Create();
        reject_allocations = true;
        pool.ReleaseExtraChunks();
        assert(pool.Create() == root);
        pool.ReleaseExtraChunks();
        reject_allocations = false;
    }
    for (int i = 0; i < 4; ++i) (void)pool.Create();
    reject_allocations = true;
    pool.ReleaseContents();
    assert(pool.Create() == root);
    pool.ReleaseExtraChunks();
    reject_allocations = false;
    for (int i = 0; i < 13; ++i) (void)pool.Create();
    pool.ReleaseContents();
    for (int i = 0; i < 9; ++i) (void)pool.Create();
}
int main() {
    Run();
    assert(Item::alive == 0);
    { std::jthread a{Run}, b{Run}; }
    assert(Item::alive == 0);
    std::puts("Shader pool: reuse, boundaries, growth, destructors, no-allocation cleanup and threads passed");
}
