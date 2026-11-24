#include "ll/alloc_counter.hpp"
#include <cstdlib>
#include <new>

namespace {
thread_local std::uint64_t g_allocs = 0;
void* do_alloc(std::size_t n) {
  ++g_allocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* do_alloc_aligned(std::size_t n, std::size_t a) {
  ++g_allocs;
  void* p = nullptr;
  if (a < sizeof(void*)) a = sizeof(void*);
  if (posix_memalign(&p, a, n ? n : 1) != 0) throw std::bad_alloc();
  return p;
}
}

namespace ll { std::uint64_t thread_alloc_count() { return g_allocs; } }

void* operator new(std::size_t n) { return do_alloc(n); }
void* operator new[](std::size_t n) { return do_alloc(n); }
void* operator new(std::size_t n, std::align_val_t a) { return do_alloc_aligned(n, (std::size_t)a); }
void* operator new[](std::size_t n, std::align_val_t a) { return do_alloc_aligned(n, (std::size_t)a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
