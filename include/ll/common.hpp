#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <vector>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace ll {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline constexpr std::size_t kCacheLine = 64;
inline constexpr u32 kNil = 0xFFFFFFFFu;

enum class Side : u8 { Buy = 0, Sell = 1 };

struct LevelSnap {
  u32 price = 0;
  u64 qty = 0;
  u32 count = 0;
  std::vector<u64> refs;
  bool operator==(const LevelSnap& o) const {
    return price == o.price && qty == o.qty && count == o.count && refs == o.refs;
  }
};

inline u64 steady_ns() {
  return (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline u64 tsc_now() {
#if defined(__x86_64__)
  _mm_lfence();
  u64 t = __rdtsc();
  _mm_lfence();
  return t;
#else
  return steady_ns();
#endif
}

inline double calibrate_ns_per_tick() {
#if defined(__x86_64__)
  u64 s0 = steady_ns();
  u64 t0 = tsc_now();
  while (steady_ns() - s0 < 100'000'000ull) {}
  u64 t1 = tsc_now();
  u64 s1 = steady_ns();
  return double(s1 - s0) / double(t1 - t0);
#else
  return 1.0;
#endif
}
inline double ns_per_tick() {
  static const double v = calibrate_ns_per_tick();
  return v;
}

inline u64 timer_overhead_ticks() {
  constexpr int N = 20001;
  std::vector<u64> s(N);
  for (int i = 0; i < N; ++i) {
    u64 a = tsc_now();
    u64 b = tsc_now();
    s[i] = b - a;
  }
  std::nth_element(s.begin(), s.begin() + N / 2, s.end());
  return s[N / 2];
}

inline bool pin_thread(int core) {
#if defined(__linux__)
  if (core < 0) return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
  (void)core;
  return false;
#endif
}

inline void cpu_relax() {
#if defined(__x86_64__)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield");
#endif
}
inline bool single_core() {
  static const bool s = std::thread::hardware_concurrency() < 2;
  return s;
}

inline void spin_wait(unsigned& n) {
  cpu_relax();
  if (single_core() && ((++n) & 63u) == 0) std::this_thread::yield();
}

inline std::size_t ceil_pow2(std::size_t x) {
  std::size_t p = 1;
  while (p < x) p <<= 1;
  return p;
}

inline void* alloc_big(std::size_t bytes, bool huge = false) {
  std::size_t align = huge ? (std::size_t(2) << 20) : kCacheLine;
  bytes = (bytes + align - 1) / align * align;
  void* p = std::aligned_alloc(align, bytes);
  if (!p) throw std::bad_alloc();
#if defined(__linux__)
  if (huge) madvise(p, bytes, MADV_HUGEPAGE);
#endif
  return p;
}
inline void free_big(void* p) { std::free(p); }

struct Rng {
  u64 s;
  explicit Rng(u64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
  u64 next() {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
  }
  u32 next32() { return (u32)(next() >> 32); }
  u32 below(u32 n) { return (u32)(((u64)next32() * n) >> 32); }
};
}
