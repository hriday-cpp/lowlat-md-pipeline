#pragma once
#include <vector>
#include "ll/common.hpp"
#include "ll/id_map.hpp"

namespace ll {

struct Order {
  u64 ref;
  u32 prev, next;
  u32 qty;
  u32 level;
  u16 sym;
  u8 side;
  u8 pad;
};
static_assert(sizeof(Order) == 32);

struct Level { u32 head, tail, count, qty; };
static_assert(sizeof(Level) == 16);

inline constexpr u32 kLadderW = 1u << 16;

struct BookConfig {
  u32 max_orders = 1u << 18;
  u32 n_symbols = 4;
  u32 tick = 100;
  bool huge = false;
};

struct SideLadder {
  Level* lv = nullptr;
  u64 l0[kLadderW / 64];
  u64 l1[kLadderW / 4096];

  void init(Level* storage) {
    lv = storage;
    for (u32 i = 0; i < kLadderW; ++i) lv[i] = Level{kNil, kNil, 0, 0};
    std::memset(l0, 0, sizeof l0);
    std::memset(l1, 0, sizeof l1);
  }
  void set(u32 i) { l0[i >> 6] |= 1ull << (i & 63); l1[i >> 12] |= 1ull << ((i >> 6) & 63); }
  void clear(u32 i) {
    u64& w = l0[i >> 6];
    w &= ~(1ull << (i & 63));
    if (!w) l1[i >> 12] &= ~(1ull << ((i >> 6) & 63));
  }
  int lowest() const {
    for (u32 k = 0; k < kLadderW / 4096; ++k)
      if (l1[k]) {
        u32 w = k * 64 + (u32)__builtin_ctzll(l1[k]);
        return (int)(w * 64 + (u32)__builtin_ctzll(l0[w]));
      }
    return -1;
  }
  int highest() const {
    for (int k = (int)(kLadderW / 4096) - 1; k >= 0; --k)
      if (l1[k]) {
        u32 w = (u32)k * 64 + (u32)(63 - __builtin_clzll(l1[k]));
        return (int)(w * 64 + (u32)(63 - __builtin_clzll(l0[w])));
      }
    return -1;
  }
};

template <class IdMap>
class BookLadderT {
 public:
  explicit BookLadderT(const BookConfig& c)
      : cfg_(c), ids_(c.max_orders, c.huge), sides_((std::size_t)c.n_symbols * 2) {
    orders_ = static_cast<Order*>(alloc_big((std::size_t)c.max_orders * sizeof(Order), c.huge));
    for (u32 i = 0; i < c.max_orders; ++i) {
      orders_[i] = Order{};
      orders_[i].next = (i + 1 < c.max_orders) ? i + 1 : kNil;
    }
    free_head_ = 0;
    std::size_t lvl_bytes = sizeof(Level) * kLadderW * sides_.size();
    levels_ = static_cast<Level*>(alloc_big(lvl_bytes, c.huge));
    for (std::size_t s = 0; s < sides_.size(); ++s) sides_[s].init(levels_ + s * kLadderW);
  }
  ~BookLadderT() { free_big(orders_); free_big(levels_); }
  BookLadderT(const BookLadderT&) = delete;
  BookLadderT& operator=(const BookLadderT&) = delete;

  bool add(u16 sym, u64 ref, Side s, u32 qty, u32 price) {
    u32 idx = price / cfg_.tick;
    if (ref == 0 || sym >= cfg_.n_symbols || idx >= kLadderW || price % cfg_.tick != 0 || free_head_ == kNil || qty == 0) {
      ++rejected_;
      return false;
    }
    u32 oi = free_head_;
    if (!ids_.insert(ref, oi)) { ++rejected_; return false; }
    free_head_ = orders_[oi].next;
    Order& o = orders_[oi];
    o.ref = ref; o.qty = qty; o.level = idx; o.sym = sym; o.side = (u8)s;
    SideLadder& sb = side(sym, s);
    Level& L = sb.lv[idx];
    o.prev = L.tail;
    o.next = kNil;
    if (L.tail != kNil) orders_[L.tail].next = oi; else L.head = oi;
    L.tail = oi;
    L.qty += qty;
    if (L.count++ == 0) sb.set(idx);
    ++live_;
    return true;
  }

  bool exec(u64 ref, u32 qty) {
    u32 oi = ids_.find(ref);
    if (oi == kNil) return false;
    Order& o = orders_[oi];
    if (qty >= o.qty) { remove(oi); return true; }
    o.qty -= qty;
    side(o.sym, (Side)o.side).lv[o.level].qty -= qty;
    return true;
  }
  bool cancel(u64 ref, u32 qty) { return exec(ref, qty); }
  bool del(u64 ref) {
    u32 oi = ids_.find(ref);
    if (oi == kNil) return false;
    remove(oi);
    return true;
  }

  bool replace(u64 old_ref, u64 new_ref, u32 qty, u32 price) {
    u32 oi = ids_.find(old_ref);
    if (oi == kNil) return false;
    u16 sym = orders_[oi].sym;
    Side s = (Side)orders_[oi].side;
    remove(oi);
    return add(sym, new_ref, s, qty, price);
  }

  u32 best_bid(u16 sym) const { int i = side(sym, Side::Buy).highest(); return i < 0 ? 0 : (u32)i * cfg_.tick; }
  u32 best_ask(u16 sym) const { int i = side(sym, Side::Sell).lowest(); return i < 0 ? 0 : (u32)i * cfg_.tick; }
  u64 live_orders() const { return live_; }
  u64 rejected() const { return rejected_; }

  std::vector<LevelSnap> snapshot(u16 sym, Side s, std::size_t n) const {
    std::vector<LevelSnap> out;
    const SideLadder& sb = side(sym, s);
    auto take = [&](u32 i) {
      const Level& L = sb.lv[i];
      LevelSnap ls;
      ls.price = i * cfg_.tick; ls.qty = L.qty; ls.count = L.count;
      for (u32 oi = L.head; oi != kNil; oi = orders_[oi].next) ls.refs.push_back(orders_[oi].ref);
      out.push_back(std::move(ls));
    };
    if (s == Side::Buy) { for (int i = (int)kLadderW - 1; i >= 0 && out.size() < n; --i) if (sb.lv[i].count) take((u32)i); }
    else { for (u32 i = 0; i < kLadderW && out.size() < n; ++i) if (sb.lv[i].count) take(i); }
    return out;
  }
  static const char* name() { return IdMap::name(); }

 private:
  SideLadder& side(u16 sym, Side s) { return sides_[(std::size_t)sym * 2 + (u8)s]; }
  const SideLadder& side(u16 sym, Side s) const { return sides_[(std::size_t)sym * 2 + (u8)s]; }

  void remove(u32 oi) {
    Order& o = orders_[oi];
    SideLadder& sb = side(o.sym, (Side)o.side);
    Level& L = sb.lv[o.level];
    if (o.prev != kNil) orders_[o.prev].next = o.next; else L.head = o.next;
    if (o.next != kNil) orders_[o.next].prev = o.prev; else L.tail = o.prev;
    L.qty -= o.qty;
    if (--L.count == 0) sb.clear(o.level);
    ids_.erase(o.ref);
    o.next = free_head_;
    free_head_ = oi;
    --live_;
  }

  BookConfig cfg_;
  IdMap ids_;
  std::vector<SideLadder> sides_;
  Order* orders_ = nullptr;
  Level* levels_ = nullptr;
  u32 free_head_ = kNil;
  u64 live_ = 0, rejected_ = 0;
};

using BookLadder = BookLadderT<FlatIdMap>;
using BookLadderStdHash = BookLadderT<StdIdMap>;
}
