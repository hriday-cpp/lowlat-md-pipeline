#pragma once
#include "ll/itch.hpp"

namespace ll {
template <class Book>
inline bool apply(Book& b, const Msg& m) {
  switch (m.type) {
    case 'A': return b.add(m.locate, m.ref, m.side == 0 ? Side::Buy : Side::Sell, m.qty, m.price);
    case 'E':
    case 'X': return b.exec(m.ref, m.qty);
    case 'D': return b.del(m.ref);
    case 'U': return b.replace(m.ref2, m.ref, m.qty, m.price);
    default: return false;
  }
}
}
