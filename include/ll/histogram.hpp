#pragma once
#include <array>
#include <cmath>
#include "ll/common.hpp"

namespace ll {
class Histogram {
 public:
  static constexpr int kSub = 16;
  static constexpr int kBuckets = 61 * kSub;

  void record(u64 v) {
    ++counts_[index(v)];
    ++total_;
    if (v > max_) max_ = v;
  }
  u64 total() const { return total_; }
  u64 max() const { return max_; }

  u64 quantile(double q) const {
    if (total_ == 0) return 0;
    u64 target = (u64)std::ceil(q * (double)total_);
    if (target < 1) target = 1;
    u64 cum = 0;
    for (int i = 0; i < kBuckets; ++i) {
      cum += counts_[i];
      if (cum >= target) return std::min(upper(i), max_);
    }
    return max_;
  }
  void merge(const Histogram& o) {
    for (int i = 0; i < kBuckets; ++i) counts_[i] += o.counts_[i];
    total_ += o.total_;
    if (o.max_ > max_) max_ = o.max_;
  }

 private:
  static int index(u64 v) {
    if (v < (u64)kSub) return (int)v;
    int e = 63 - __builtin_clzll(v);
    int sub = (int)((v >> (e - 4)) & 15);
    return (e - 3) * kSub + sub;
  }
  static u64 upper(int i) {
    if (i < kSub) return (u64)i;
    int e = i / kSub + 3;
    int sub = i % kSub;
    u64 lo = (u64)(kSub + sub) << (e - 4);
    return lo + ((u64)1 << (e - 4)) - 1;
  }
  std::array<u64, kBuckets> counts_{};
  u64 total_ = 0;
  u64 max_ = 0;
};
}
