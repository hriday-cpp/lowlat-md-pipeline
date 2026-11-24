#pragma once
#include "ll/common.hpp"

namespace ll {

struct alignas(kCacheLine) Msg {
  u64 t0;
  u64 ref;
  u64 ref2;
  u32 qty;
  u32 price;
  u16 locate;
  u8 type;
  u8 side;
  u8 pad[28];
};
static_assert(sizeof(Msg) == 64);
inline constexpr u8 kStop = 0xFF;

namespace itch {

inline u16 be16(const u8* p) { u16 v; std::memcpy(&v, p, 2); return __builtin_bswap16(v); }
inline u32 be32(const u8* p) { u32 v; std::memcpy(&v, p, 4); return __builtin_bswap32(v); }
inline u64 be64(const u8* p) { u64 v; std::memcpy(&v, p, 8); return __builtin_bswap64(v); }
inline u64 be48(const u8* p) { return ((u64)be16(p) << 32) | be32(p + 2); }
inline void put16(u8* p, u16 v) { v = __builtin_bswap16(v); std::memcpy(p, &v, 2); }
inline void put32(u8* p, u32 v) { v = __builtin_bswap32(v); std::memcpy(p, &v, 4); }
inline void put64(u8* p, u64 v) { v = __builtin_bswap64(v); std::memcpy(p, &v, 8); }
inline void put48(u8* p, u64 v) { put16(p, (u16)(v >> 32)); put32(p + 2, (u32)v); }

inline constexpr std::size_t kLenA = 36, kLenE = 31, kLenX = 23, kLenD = 19, kLenU = 35;

inline bool parse_msg(const u8* p, std::size_t len, Msg& m) {
  switch (p[0]) {
    case 'A':
      if (len < kLenA) return false;
      m.type = 'A'; m.locate = be16(p + 1); m.ref = be64(p + 11);
      m.side = (p[19] == 'B') ? 0 : 1; m.qty = be32(p + 20); m.price = be32(p + 32);
      return true;
    case 'E':
      if (len < kLenE) return false;
      m.type = 'E'; m.locate = be16(p + 1); m.ref = be64(p + 11); m.qty = be32(p + 19);
      return true;
    case 'X':
      if (len < kLenX) return false;
      m.type = 'X'; m.locate = be16(p + 1); m.ref = be64(p + 11); m.qty = be32(p + 19);
      return true;
    case 'D':
      if (len < kLenD) return false;
      m.type = 'D'; m.locate = be16(p + 1); m.ref = be64(p + 11);
      return true;
    case 'U':
      if (len < kLenU) return false;
      m.type = 'U'; m.locate = be16(p + 1); m.ref2 = be64(p + 11); m.ref = be64(p + 19);
      m.qty = be32(p + 27); m.price = be32(p + 31);
      return true;
    default:
      return false;
  }
}

struct FrameReader {
  const u8* p;
  const u8* end;
  bool next(const u8*& body, std::size_t& len) {
    if ((std::size_t)(end - p) < 2) return false;
    len = be16(p);
    p += 2;
    if ((std::size_t)(end - p) < len) return false;
    body = p;
    p += len;
    return true;
  }
};

inline std::size_t encode(const Msg& m, u64 ts, u8* out) {
  u8* b = out + 2;
  std::size_t len = 0;
  std::memset(b, 0, 40);
  b[0] = m.type;
  put16(b + 1, m.locate);
  put48(b + 5, ts);
  switch (m.type) {
    case 'A':
      put64(b + 11, m.ref); b[19] = m.side == 0 ? 'B' : 'S'; put32(b + 20, m.qty);
      std::memcpy(b + 24, "SYM     ", 8); put32(b + 32, m.price); len = kLenA; break;
    case 'E': put64(b + 11, m.ref); put32(b + 19, m.qty); put64(b + 23, ts); len = kLenE; break;
    case 'X': put64(b + 11, m.ref); put32(b + 19, m.qty); len = kLenX; break;
    case 'D': put64(b + 11, m.ref); len = kLenD; break;
    case 'U':
      put64(b + 11, m.ref2); put64(b + 19, m.ref); put32(b + 27, m.qty); put32(b + 31, m.price); len = kLenU; break;
    default: return 0;
  }
  put16(out, (u16)len);
  return len + 2;
}
}
}
