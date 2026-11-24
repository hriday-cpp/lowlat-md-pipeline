#include <thread>
#include "check.hpp"
#include "ll/itch.hpp"
#include "ll/spsc.hpp"
using namespace ll;

template <class Q>
static void run(const char* name, u64 n, std::size_t cap) {
  Q q(cap);
  u64 sum = 0, expect = 0; bool ordered = true;
  std::thread cons([&] {
    Msg m; unsigned sp = 0;
    for (u64 i = 0; i < n; ++i) {
      while (!q.try_pop(m)) spin_wait(sp);
      if (m.ref != i) ordered = false;
      sum += m.ref;
    }
  });
  std::thread prod([&] {
    Msg m{}; unsigned sp = 0;
    for (u64 i = 0; i < n; ++i) { m.ref = i; expect += i; while (!q.try_push(m)) spin_wait(sp); }
  });
  prod.join(); cons.join();
  CHECK_MSG(ordered, "%s out of order", name);
  CHECK_MSG(sum == expect, "%s lost/duplicated items", name);
  Msg x; CHECK(!q.try_pop(x));
}

int main() {
  const u64 n = 1'000'000;
  run<MutexQueue<Msg>>("mutex", n / 4, 1024);
  run<NaiveSpsc<Msg>>("naive", n, 1024);
  run<PaddedSpsc<Msg>>("padded", n, 1024);
  run<CachedSpsc<Msg>>("cached", n, 1024);
  run<CachedSpsc<Msg>>("cached-tiny", n, 2);

  CachedSpsc<Msg> q(64);
  Msg m{}; for (u64 i = 0; i < 40; ++i) { m.ref = i; CHECK(q.try_push(m)); }
  Msg out[16]; u64 got = 0;
  for (;;) { std::size_t k = q.try_pop_batch(out, 16); if (!k) break; for (std::size_t i = 0; i < k; ++i) CHECK(out[i].ref == got + i); got += k; }
  CHECK(got == 40);
  DONE("test_spsc");
}
