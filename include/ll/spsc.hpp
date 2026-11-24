#pragma once
#include <atomic>
#include <mutex>
#include <type_traits>
#include "ll/common.hpp"

namespace ll {

template <class T>
class MutexQueue {
  static_assert(std::is_trivially_copyable_v<T>);
 public:
  explicit MutexQueue(std::size_t cap) : cap_(ceil_pow2(cap)), mask_(cap_ - 1), buf_(cap_) {}
  bool try_push(const T& v) {
    std::lock_guard<std::mutex> g(m_);
    if (head_ - tail_ == cap_) return false;
    buf_[head_ & mask_] = v;
    ++head_;
    return true;
  }
  bool try_pop(T& v) {
    std::lock_guard<std::mutex> g(m_);
    if (head_ == tail_) return false;
    v = buf_[tail_ & mask_];
    ++tail_;
    return true;
  }
  static const char* name() { return "mutex"; }
 private:
  std::size_t cap_, mask_;
  std::vector<T> buf_;
  std::mutex m_;
  std::size_t head_ = 0, tail_ = 0;
};

template <class T>
class NaiveSpsc {
  static_assert(std::is_trivially_copyable_v<T>);
 public:
  explicit NaiveSpsc(std::size_t cap) : cap_(ceil_pow2(cap)), mask_(cap_ - 1), buf_(alloc_big(cap_ * sizeof(T))) {}
  ~NaiveSpsc() { free_big(buf_); }
  NaiveSpsc(const NaiveSpsc&) = delete;
  NaiveSpsc& operator=(const NaiveSpsc&) = delete;
  bool try_push(const T& v) {
    std::size_t h = head_.load();
    if (h - tail_.load() == cap_) return false;
    std::memcpy(static_cast<T*>(buf_) + (h & mask_), &v, sizeof(T));
    head_.store(h + 1);
    return true;
  }
  bool try_pop(T& v) {
    std::size_t t = tail_.load();
    if (t == head_.load()) return false;
    std::memcpy(&v, static_cast<T*>(buf_) + (t & mask_), sizeof(T));
    tail_.store(t + 1);
    return true;
  }
  static const char* name() { return "naive"; }
 private:
  std::size_t cap_, mask_;
  void* buf_;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

template <class T>
class PaddedSpsc {
  static_assert(std::is_trivially_copyable_v<T>);
 public:
  explicit PaddedSpsc(std::size_t cap) : cap_(ceil_pow2(cap)), mask_(cap_ - 1), buf_(alloc_big(cap_ * sizeof(T))) {}
  ~PaddedSpsc() { free_big(buf_); }
  PaddedSpsc(const PaddedSpsc&) = delete;
  PaddedSpsc& operator=(const PaddedSpsc&) = delete;
  bool try_push(const T& v) {
    std::size_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) == cap_) return false;
    std::memcpy(static_cast<T*>(buf_) + (h & mask_), &v, sizeof(T));
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  bool try_pop(T& v) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    std::memcpy(&v, static_cast<T*>(buf_) + (t & mask_), sizeof(T));
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }
  static const char* name() { return "padded"; }
 private:
  std::size_t cap_, mask_;
  void* buf_;
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
  char pad_[kCacheLine - sizeof(std::atomic<std::size_t>)];
};

template <class T>
class CachedSpsc {
  static_assert(std::is_trivially_copyable_v<T>);
 public:
  explicit CachedSpsc(std::size_t cap) : cap_(ceil_pow2(cap)), mask_(cap_ - 1), buf_(alloc_big(cap_ * sizeof(T))) {}
  ~CachedSpsc() { free_big(buf_); }
  CachedSpsc(const CachedSpsc&) = delete;
  CachedSpsc& operator=(const CachedSpsc&) = delete;
  bool try_push(const T& v) {
    std::size_t h = head_.load(std::memory_order_relaxed);
    if (h - cached_tail_ == cap_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (h - cached_tail_ == cap_) return false;
    }
    std::memcpy(static_cast<T*>(buf_) + (h & mask_), &v, sizeof(T));
    head_.store(h + 1, std::memory_order_release);
    return true;
  }
  bool try_pop(T& v) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (t == cached_head_) return false;
    }
    std::memcpy(&v, static_cast<T*>(buf_) + (t & mask_), sizeof(T));
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }

  std::size_t try_pop_batch(T* out, std::size_t max) {
    std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (t == cached_head_) return 0;
    }
    std::size_t n = std::min<std::size_t>(cached_head_ - t, max);
    for (std::size_t i = 0; i < n; ++i)
      std::memcpy(out + i, static_cast<T*>(buf_) + ((t + i) & mask_), sizeof(T));
    tail_.store(t + n, std::memory_order_release);
    return n;
  }
  static const char* name() { return "cached"; }
 private:
  std::size_t cap_, mask_;
  void* buf_;

  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  std::size_t cached_tail_ = 0;

  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
  std::size_t cached_head_ = 0;
  char pad_[kCacheLine - sizeof(std::atomic<std::size_t>) - sizeof(std::size_t)];
};

}
