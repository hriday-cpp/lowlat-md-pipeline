#include "check.hpp"
#include "ll/apply.hpp"
#include "ll/book_map.hpp"
#include "ll/flowgen.hpp"
using namespace ll;

static void compare_snaps(const BookMap& a, const BookLadder& b, const BookLadderStdHash& c, u32 syms, std::size_t n, std::size_t step) {
  for (u32 s = 0; s < syms; ++s)
    for (Side sd : {Side::Buy, Side::Sell}) {
      auto sa = a.snapshot((u16)s, sd, n), sb = b.snapshot((u16)s, sd, n), sc = c.snapshot((u16)s, sd, n);
      CHECK_MSG(sa == sb, "ladder != map at step %zu sym %u side %d", step, s, (int)sd);
      CHECK_MSG(sa == sc, "ladder(std hash) != map at step %zu", step);
    }
}

int main() {
  for (u64 seed : {1ull, 7ull, 12345ull}) {
    FlowConfig fc; fc.seed = seed; fc.target_live = 20000;
    const std::size_t N = 600000;
    FlowGen g(fc, N);
    BookConfig bc; bc.max_orders = 1 << 17; bc.n_symbols = fc.n_syms;
    BookMap ref(bc); BookLadder lad(bc); BookLadderStdHash lad2(bc);
    Rng junk(seed * 31 + 1);
    for (std::size_t i = 0; i < N; ++i) {
      Msg m = g.next();
      bool r0 = apply(ref, m), r1 = apply(lad, m), r2 = apply(lad2, m);
      CHECK_MSG(r0 == r1 && r0 == r2, "return mismatch at %zu type %c", i, m.type);
      if ((i % 97) == 0) {
        Msg bad = m;
        switch (junk.below(4)) {
          case 0: bad.type = 'D'; bad.ref = 0xFFFFFFF0ull + junk.below(100); break;
          case 1: bad.type = 'E'; bad.ref = 0xFFFFFFF0ull + junk.below(100); break;
          case 2: if (m.type == 'A') { bad.price += 1; } break;
          default: if (m.type == 'A') { bad.qty = 0; } break;
        }
        bool b0 = apply(ref, bad), b1 = apply(lad, bad), b2 = apply(lad2, bad);
        CHECK_MSG(b0 == b1 && b0 == b2, "invalid-op mismatch at %zu", i);
      }
      if ((i % 1000) == 0) {
        for (u32 s = 0; s < fc.n_syms; ++s) {
          CHECK(ref.best_bid((u16)s) == lad.best_bid((u16)s));
          CHECK(ref.best_ask((u16)s) == lad.best_ask((u16)s));
          CHECK(ref.best_bid((u16)s) == lad2.best_bid((u16)s));
        }
        CHECK(ref.live_orders() == lad.live_orders());
        compare_snaps(ref, lad, lad2, fc.n_syms, 5, i);
      }
      if (g_fail > 10) break;
    }
    compare_snaps(ref, lad, lad2, fc.n_syms, 1u << 30, N);
    CHECK(ref.live_orders() == lad.live_orders());
    CHECK(ref.rejected() == lad.rejected());
    std::printf("seed %llu: live=%llu rejected=%llu\n", (unsigned long long)seed,
                (unsigned long long)lad.live_orders(), (unsigned long long)lad.rejected());
  }
  DONE("test_book_diff");
}
