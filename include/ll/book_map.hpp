#pragma once
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include "ll/book_ladder.hpp"

namespace ll {

class BookMap {
 public:
  explicit BookMap(const BookConfig& c) : cfg_(c), bids_(c.n_symbols), asks_(c.n_symbols) {
    ids_.reserve(c.max_orders);
  }

  bool add(u16 sym, u64 ref, Side s, u32 qty, u32 price) {
    if (ref == 0 || sym >= cfg_.n_symbols || price % cfg_.tick != 0 || price / cfg_.tick >= kLadderW || qty == 0 ||
        ids_.count(ref) || live_ >= cfg_.max_orders) {
      ++rejected_;
      return false;
    }
    Ord o{ref, qty, price, sym, (u8)s};
    List& l = (s == Side::Buy) ? bids_[sym][price] : asks_[sym][price];
    l.push_back(o);
    ids_.emplace(ref, std::prev(l.end()));
    ++live_;
    return true;
  }
  bool exec(u64 ref, u32 qty) {
    auto it = ids_.find(ref);
    if (it == ids_.end()) return false;
    if (qty >= it->second->qty) { remove(it); return true; }
    it->second->qty -= qty;
    return true;
  }
  bool cancel(u64 ref, u32 qty) { return exec(ref, qty); }
  bool del(u64 ref) {
    auto it = ids_.find(ref);
    if (it == ids_.end()) return false;
    remove(it);
    return true;
  }
  bool replace(u64 old_ref, u64 new_ref, u32 qty, u32 price) {
    auto it = ids_.find(old_ref);
    if (it == ids_.end()) return false;
    u16 sym = it->second->sym;
    Side s = (Side)it->second->side;
    remove(it);
    return add(sym, new_ref, s, qty, price);
  }
  u32 best_bid(u16 sym) const { return bids_[sym].empty() ? 0 : bids_[sym].begin()->first; }
  u32 best_ask(u16 sym) const { return asks_[sym].empty() ? 0 : asks_[sym].begin()->first; }
  u64 live_orders() const { return live_; }
  u64 rejected() const { return rejected_; }

  std::vector<LevelSnap> snapshot(u16 sym, Side s, std::size_t n) const {
    std::vector<LevelSnap> out;
    auto emit = [&](const auto& m) {
      for (const auto& [price, lst] : m) {
        if (out.size() >= n) break;
        LevelSnap ls;
        ls.price = price; ls.count = (u32)lst.size();
        for (const Ord& o : lst) { ls.qty += o.qty; ls.refs.push_back(o.ref); }
        out.push_back(std::move(ls));
      }
    };
    if (s == Side::Buy) emit(bids_[sym]); else emit(asks_[sym]);
    return out;
  }
  static const char* name() { return "std::map+list+unordered_map"; }

 private:
  struct Ord { u64 ref; u32 qty; u32 price; u16 sym; u8 side; };
  using List = std::list<Ord>;
  using Ids = std::unordered_map<u64, List::iterator>;

  void remove(Ids::iterator it) {
    const Ord& o = *it->second;
    if (o.side == 0) erase_from(bids_[o.sym], o.price, it->second);
    else erase_from(asks_[o.sym], o.price, it->second);
    ids_.erase(it);
    --live_;
  }
  template <class M>
  void erase_from(M& m, u32 price, List::iterator lit) {
    auto mit = m.find(price);
    mit->second.erase(lit);
    if (mit->second.empty()) m.erase(mit);
  }

  BookConfig cfg_;
  std::vector<std::map<u32, List, std::greater<u32>>> bids_;
  std::vector<std::map<u32, List>> asks_;
  Ids ids_;
  u64 live_ = 0, rejected_ = 0;
};
}
