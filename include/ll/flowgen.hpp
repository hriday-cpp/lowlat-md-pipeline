#pragma once
#include <vector>
#include "ll/common.hpp"
#include "ll/itch.hpp"

namespace ll {

struct FlowConfig {
  u32 n_syms = 4;
  u64 seed = 1;
  u32 tick = 100;
  u32 mid_idx = 10000;
  u32 target_live = 200000;
};

class FlowGen {
 public:
  FlowGen(const FlowConfig& c, std::size_t n_msgs)
      : c_(c), rng_(c.seed), mid_(c.n_syms, c.mid_idx), pos_(n_msgs + 8, kNil) {
    live_.reserve(c.target_live + 1024);
  }

  Msg next() {
    Msg m{};
    ++n_;
    if ((n_ & 255) == 0) {
      u32 s = rng_.below(c_.n_syms);
      u32 r = rng_.below(2);
      if (r && mid_[s] < 30000) ++mid_[s];
      else if (!r && mid_[s] > 5000) --mid_[s];
    }
    ts_ += 100 + rng_.below(1900);
    m.t0 = ts_;

    const std::size_t n = live_.size();
    int op;
    if (n < 1000) {
      op = 0;
    } else {
      bool low = n < c_.target_live;
      u32 r = rng_.below(100);
      u32 add_w = low ? 44 : 20, del_w = low ? 30 : 54;
      if (r < add_w) op = 0;
      else if (r < add_w + del_w) op = 1;
      else if (r < add_w + del_w + 10) op = 2;
      else if (r < add_w + del_w + 18) op = 3;
      else op = 4;
    }
    switch (op) {
      case 0: return make_add(m);
      case 1: return make_delete(m);
      case 2: return make_exec(m);
      case 3: return make_cancel(m);
      default: return make_replace(m);
    }
  }

 private:
  struct Live { u64 ref; u32 price; u32 qty; u16 sym; u8 side; };

  u32 offset() {
    u32 off = 0;
    while ((rng_.next32() & 3) != 0 && off < 200) ++off;
    return off;
  }
  u32 price_for(u16 sym, u8 side) {
    u32 off = offset();
    u32 idx = side == 0 ? mid_[sym] - off : mid_[sym] + 1 + off;
    return idx * c_.tick;
  }
  void remove_at(std::size_t i) {
    pos_[live_[i].ref] = kNil;
    if (i + 1 != live_.size()) {
      live_[i] = live_.back();
      pos_[live_[i].ref] = (u32)i;
    }
    live_.pop_back();
  }
  Msg& make_add(Msg& m) {
    Live o{next_ref_++, 0, 100 * (1 + rng_.below(10)), (u16)rng_.below(c_.n_syms), (u8)(rng_.next32() & 1)};
    o.price = price_for(o.sym, o.side);
    pos_[o.ref] = (u32)live_.size();
    live_.push_back(o);
    m.type = 'A'; m.locate = o.sym; m.ref = o.ref; m.side = o.side; m.qty = o.qty; m.price = o.price;
    return m;
  }
  Msg& make_delete(Msg& m) {
    std::size_t i = rng_.below((u32)live_.size());
    Live o = live_[i];
    remove_at(i);
    m.type = 'D'; m.locate = o.sym; m.ref = o.ref;
    return m;
  }
  Msg& make_exec(Msg& m) {
    std::size_t best = rng_.below((u32)live_.size());
    for (int k = 0; k < 3; ++k) {
      std::size_t j = rng_.below((u32)live_.size());
      if (dist(live_[j]) < dist(live_[best])) best = j;
    }
    Live& o = live_[best];
    u32 lots = o.qty / 100;
    u32 q = 100 * (1 + rng_.below(lots));
    m.type = 'E'; m.locate = o.sym; m.ref = o.ref; m.qty = q;
    if (q >= o.qty) remove_at(best); else o.qty -= q;
    return m;
  }
  Msg& make_cancel(Msg& m) {
    for (int k = 0; k < 4; ++k) {
      std::size_t i = rng_.below((u32)live_.size());
      Live& o = live_[i];
      if (o.qty >= 200) {
        u32 lots = o.qty / 100;
        u32 q = 100 * (1 + rng_.below(lots - 1));
        m.type = 'X'; m.locate = o.sym; m.ref = o.ref; m.qty = q;
        o.qty -= q;
        return m;
      }
    }
    return make_delete(m);
  }
  Msg& make_replace(Msg& m) {
    std::size_t i = rng_.below((u32)live_.size());
    Live old = live_[i];
    Live o{next_ref_++, 0, 100 * (1 + rng_.below(10)), old.sym, old.side};
    o.price = price_for(o.sym, o.side);
    pos_[old.ref] = kNil;
    pos_[o.ref] = (u32)i;
    live_[i] = o;
    m.type = 'U'; m.locate = o.sym; m.ref2 = old.ref; m.ref = o.ref; m.qty = o.qty; m.price = o.price;
    return m;
  }
  u64 dist(const Live& o) const {
    u64 mid = (u64)mid_[o.sym] * c_.tick;
    return o.price > mid ? o.price - mid : mid - o.price;
  }

  FlowConfig c_;
  Rng rng_;
  std::vector<u32> mid_;
  std::vector<u32> pos_;
  std::vector<Live> live_;
  u64 next_ref_ = 1;
  u64 ts_ = 34'200'000'000'000ull;
  u64 n_ = 0;
};
}
