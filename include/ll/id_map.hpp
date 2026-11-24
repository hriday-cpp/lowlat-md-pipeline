#pragma once
#include <unordered_map>
#include "ll/common.hpp"

namespace ll {

class FlatIdMap {
 public:
  FlatIdMap(u32 max_orders, bool huge) {
    std::size_t cap = 2;
    bits_ = 1;
    while (cap < 2ull * max_orders) { cap <<= 1; ++bits_; }
    mask_ = cap - 1;
    slots_ = static_cast<Slot*>(alloc_big(cap * sizeof(Slot), huge));
    std::memset(slots_, 0, cap * sizeof(Slot));
  }
  ~FlatIdMap() { free_big(slots_); }
  FlatIdMap(const FlatIdMap&) = delete;
  FlatIdMap& operator=(const FlatIdMap&) = delete;

  u32 find(u64 k) const {
    for (std::size_t i = home(k);; i = (i + 1) & mask_) {
      const Slot& s = slots_[i];
      if (s.key == k) return s.val;
      if (s.key == 0) return kNil;
    }
  }
  bool insert(u64 k, u32 v) {
    for (std::size_t i = home(k);; i = (i + 1) & mask_) {
      Slot& s = slots_[i];
      if (s.key == 0) { s.key = k; s.val = v; return true; }
      if (s.key == k) return false;
    }
  }
  void erase(u64 k) {
    std::size_t i = home(k);
    while (slots_[i].key != k) {
      if (slots_[i].key == 0) return;
      i = (i + 1) & mask_;
    }
    std::size_t j = i;
    for (;;) {
      j = (j + 1) & mask_;
      if (slots_[j].key == 0) break;
      std::size_t h = home(slots_[j].key);
      bool in_gap = (i <= j) ? (i < h && h <= j) : (i < h || h <= j);
      if (in_gap) continue;
      slots_[i] = slots_[j];
      i = j;
    }
    slots_[i].key = 0;
  }
  static const char* name() { return "flat-hash"; }

 private:
  struct Slot { u64 key; u32 val; u32 pad; };
  std::size_t home(u64 k) const { return (std::size_t)((k * 0x9E3779B97F4A7C15ull) >> (64 - bits_)); }
  Slot* slots_ = nullptr;
  std::size_t mask_ = 0;
  unsigned bits_ = 1;
};

class StdIdMap {
 public:
  StdIdMap(u32 max_orders, bool) { m_.reserve(max_orders); }
  u32 find(u64 k) const { auto it = m_.find(k); return it == m_.end() ? kNil : it->second; }
  bool insert(u64 k, u32 v) { return m_.emplace(k, v).second; }
  void erase(u64 k) { m_.erase(k); }
  static const char* name() { return "std-unordered_map"; }
 private:
  std::unordered_map<u64, u32> m_;
};
}
