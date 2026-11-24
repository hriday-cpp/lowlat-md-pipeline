#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "ll/histogram.hpp"
#include "ll/itch.hpp"
#include "ll/spsc.hpp"
using namespace ll;

struct Opts { u64 n = 20'000'000; int runs = 3; u64 pp = 500'000; int pin_p = -1, pin_c = -1; std::string csv; };

template <class Q>
static double throughput(const Opts& o) {
  Q q(1 << 14);
  std::atomic<int> ready{0}; std::atomic<bool> go{false};
  u64 sink = 0;
  std::thread cons([&] {
    pin_thread(o.pin_c); ready++; unsigned sp = 0; while (!go) spin_wait(sp);
    Msg m;
    for (u64 i = 0; i < o.n; ++i) { sp = 0; while (!q.try_pop(m)) spin_wait(sp); sink += m.ref; }
  });
  std::thread prod([&] {
    pin_thread(o.pin_p); ready++; unsigned sp = 0; while (!go) spin_wait(sp);
    Msg m{};
    for (u64 i = 0; i < o.n; ++i) { m.ref = i; sp = 0; while (!q.try_push(m)) spin_wait(sp); }
  });
  while (ready < 2) std::this_thread::yield();
  u64 t0 = steady_ns(); go = true; cons.join(); prod.join();
  double secs = (steady_ns() - t0) / 1e9;
  if (sink == 0xDEADBEEF) std::puts("");
  return o.n / secs / 1e6;
}

static double throughput_batch(const Opts& o) {
  CachedSpsc<Msg> q(1 << 14);
  std::atomic<int> ready{0}; std::atomic<bool> go{false};
  u64 sink = 0;
  std::thread cons([&] {
    pin_thread(o.pin_c); ready++; unsigned sp = 0; while (!go) spin_wait(sp);
    Msg buf[32]; u64 got = 0;
    while (got < o.n) { std::size_t k = q.try_pop_batch(buf, 32); if (!k) { spin_wait(sp); continue; } for (std::size_t i = 0; i < k; ++i) sink += buf[i].ref; got += k; }
  });
  std::thread prod([&] {
    pin_thread(o.pin_p); ready++; unsigned sp = 0; while (!go) spin_wait(sp);
    Msg m{};
    for (u64 i = 0; i < o.n; ++i) { m.ref = i; sp = 0; while (!q.try_push(m)) spin_wait(sp); }
  });
  while (ready < 2) std::this_thread::yield();
  u64 t0 = steady_ns(); go = true; cons.join(); prod.join();
  if (sink == 0xDEADBEEF) std::puts("");
  return o.n / ((steady_ns() - t0) / 1e9) / 1e6;
}

template <class Q>
static Histogram pingpong(const Opts& o) {
  Q a(1024), b(1024);
  std::atomic<bool> ready{false}, stop{false};
  std::thread echo([&] {
    pin_thread(o.pin_c); ready = true; Msg m; unsigned sp = 0;
    for (u64 i = 0; i < o.pp; ++i) { sp = 0; while (!a.try_pop(m)) spin_wait(sp); sp = 0; while (!b.try_push(m)) spin_wait(sp); }
  });
  pin_thread(o.pin_p);
  while (!ready) std::this_thread::yield();
  Histogram h; Msg m{}, r; unsigned sp = 0;
  for (u64 i = 0; i < o.pp; ++i) {
    m.ref = i;
    u64 t0 = tsc_now();
    sp = 0; while (!a.try_push(m)) spin_wait(sp);
    sp = 0; while (!b.try_pop(r)) spin_wait(sp);
    u64 t1 = tsc_now();
    h.record((t1 - t0) / 2);
  }
  echo.join();
  return h;
}

template <class Q>
static void run(const Opts& o, std::ofstream& csv) {
  std::vector<double> t;
  for (int r = 0; r < o.runs; ++r) t.push_back(throughput<Q>(o));
  std::sort(t.begin(), t.end());
  Histogram h = pingpong<Q>(o);
  double npt = ns_per_tick();
  std::printf("%-10s %8.2f Mmsg/s (min %.2f max %.2f) | one-way ns: p50=%.0f p99=%.0f p99.9=%.0f max=%.0f\n", Q::name(),
              t[t.size() / 2], t.front(), t.back(), h.quantile(.5) * npt, h.quantile(.99) * npt, h.quantile(.999) * npt, h.max() * npt);
  if (csv.is_open()) csv << Q::name() << ',' << t[t.size() / 2] << ',' << t.front() << ',' << t.back() << ',' << h.quantile(.5) * npt << ',' << h.quantile(.99) * npt << ',' << h.quantile(.999) * npt << '\n';
}

int main(int argc, char** argv) {
  Opts o; int pos = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--csv" && i + 1 < argc) o.csv = argv[++i];
    else if (a == "--pin" && i + 1 < argc) { std::string s = argv[++i]; auto c = s.find(','); o.pin_p = std::atoi(s.c_str()); o.pin_c = c == std::string::npos ? -1 : std::atoi(s.c_str() + c + 1); }
    else if (pos == 0) { o.n = std::strtoull(argv[i], nullptr, 10); ++pos; }
    else if (pos == 1) { o.runs = std::atoi(argv[i]); ++pos; }
    else if (pos == 2) { o.pp = std::strtoull(argv[i], nullptr, 10); ++pos; }
  }
  std::printf("n=%llu runs=%d pingpong=%llu | pin=%d,%d | hw threads=%u | tsc %.4f ns/tick\n", (unsigned long long)o.n, o.runs,
              (unsigned long long)o.pp, o.pin_p, o.pin_c, std::thread::hardware_concurrency(), ns_per_tick());
  if (single_core()) std::puts("WARNING: single hardware thread: cross-core numbers are meaningless here. Run on a multi-core Linux box.");
  std::ofstream csv;
  if (!o.csv.empty()) { csv.open(o.csv); csv << "queue,mmsgs_per_s_median,min,max,oneway_p50_ns,oneway_p99_ns,oneway_p999_ns\n"; }
  run<MutexQueue<Msg>>(o, csv);
  run<NaiveSpsc<Msg>>(o, csv);
  run<PaddedSpsc<Msg>>(o, csv);
  run<CachedSpsc<Msg>>(o, csv);
  double bt = 0; { std::vector<double> t; for (int r = 0; r < o.runs; ++r) t.push_back(throughput_batch(o)); std::sort(t.begin(), t.end()); bt = t[t.size() / 2]; }
  std::printf("%-10s %8.2f Mmsg/s (consumer pops in batches of 32)\n", "cached+bat", bt);
  if (csv.is_open()) csv << "cached+batch," << bt << ",,,,,\n";
}
