#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ll/alloc_counter.hpp"
#include "ll/apply.hpp"
#include "ll/book_map.hpp"
#include "ll/flowgen.hpp"
#include "ll/histogram.hpp"
#include "ll/spsc.hpp"
using namespace ll;

struct Opts {
  std::string file, queue = "cached", book = "ladder", csv;
  u64 rate = 0; int pin_p = -1, pin_c = -1; u32 cap = 1u << 16; int runs = 3; u32 symbols = 4; u32 max_orders = 1u << 18;
  std::size_t gen = 0; bool timing = true, huge = false;
};
struct Data { const u8* p = nullptr; std::size_t n = 0; };
struct Result {
  u64 msgs = 0, rejected = 0, stalls = 0, allocs_c = 0, allocs_p = 0, checksum = 0;
  double secs = 0; bool pin_p = false, pin_c = false;
  Histogram e2e, apply;
};

template <class Q, class B>
static Result run_once(const Data& d, const Opts& o, u64 ovh) {
  BookConfig bc; bc.n_symbols = o.symbols; bc.huge = o.huge; bc.max_orders = o.max_orders;
  Q q(o.cap);
  B book(bc);
  Result r;
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  const double npt = ns_per_tick();

  std::thread cons([&] {
    r.pin_c = pin_thread(o.pin_c);
    ready.fetch_add(1);
    unsigned sp = 0;
    while (!go.load(std::memory_order_acquire)) spin_wait(sp);
    Msg m;
    u64 a0 = thread_alloc_count();
    for (;;) {
      sp = 0;
      while (!q.try_pop(m)) spin_wait(sp);
      if (m.type == kStop) break;
      if (o.timing) {
        u64 ta = tsc_now();
        apply(book, m);
        u64 t1 = tsc_now();
        u64 dt = t1 - ta;
        r.apply.record(dt > ovh ? dt - ovh : 0);
        r.e2e.record(t1 - m.t0);
      } else {
        apply(book, m);
      }
      ++r.msgs;
    }
    r.allocs_c = thread_alloc_count() - a0;
  });
  std::thread prod([&] {
    r.pin_p = pin_thread(o.pin_p);
    ready.fetch_add(1);
    unsigned sp = 0;
    while (!go.load(std::memory_order_acquire)) spin_wait(sp);
    u64 a0 = thread_alloc_count();
    itch::FrameReader fr{d.p, d.p + d.n};
    const u8* body; std::size_t len;
    Msg m{};
    const u64 interval = o.rate ? (u64)(1e9 / (double)o.rate / npt) : 0;
    u64 due = tsc_now();
    while (fr.next(body, len)) {
      if (interval) { due += interval; while (tsc_now() < due) cpu_relax(); }
      u64 t0 = o.timing ? tsc_now() : 0;
      if (!itch::parse_msg(body, len, m)) continue;
      m.t0 = t0;
      sp = 0;
      while (!q.try_push(m)) { ++r.stalls; spin_wait(sp); }
    }
    Msg stop{}; stop.type = kStop;
    sp = 0;
    while (!q.try_push(stop)) spin_wait(sp);
    r.allocs_p = thread_alloc_count() - a0;
  });
  while (ready.load() < 2) std::this_thread::yield();
  u64 t_start = steady_ns();
  go.store(true, std::memory_order_release);
  cons.join(); prod.join();
  r.secs = (steady_ns() - t_start) / 1e9;
  r.rejected = book.rejected();
  u64 h = 1469598103934665603ull;
  auto mix = [&](u64 v) { h ^= v; h *= 1099511628211ull; };
  for (u32 s = 0; s < o.symbols; ++s) { mix(book.best_bid((u16)s)); mix(book.best_ask((u16)s)); }
  mix(book.live_orders());
  r.checksum = h;
  return r;
}

template <class Q>
static Result dispatch_book(const Data& d, const Opts& o, u64 ovh) {
  if (o.book == "map") return run_once<Q, BookMap>(d, o, ovh);
  if (o.book == "stdhash") return run_once<Q, BookLadderStdHash>(d, o, ovh);
  return run_once<Q, BookLadder>(d, o, ovh);
}
static Result dispatch(const Data& d, const Opts& o, u64 ovh) {
  if (o.queue == "mutex") return dispatch_book<MutexQueue<Msg>>(d, o, ovh);
  if (o.queue == "naive") return dispatch_book<NaiveSpsc<Msg>>(d, o, ovh);
  if (o.queue == "padded") return dispatch_book<PaddedSpsc<Msg>>(d, o, ovh);
  return dispatch_book<CachedSpsc<Msg>>(d, o, ovh);
}

int main(int argc, char** argv) {
  Opts o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--file") o.file = val();
    else if (a == "--gen") o.gen = std::strtoull(val().c_str(), nullptr, 10);
    else if (a == "--queue") o.queue = val();
    else if (a == "--book") o.book = val();
    else if (a == "--rate") o.rate = std::strtoull(val().c_str(), nullptr, 10);
    else if (a == "--cap") o.cap = (u32)std::strtoul(val().c_str(), nullptr, 10);
    else if (a == "--runs") o.runs = std::atoi(val().c_str());
    else if (a == "--max-orders") o.max_orders = (u32)std::strtoul(val().c_str(), nullptr, 10);
    else if (a == "--symbols") o.symbols = (u32)std::strtoul(val().c_str(), nullptr, 10);
    else if (a == "--csv") o.csv = val();
    else if (a == "--no-timing") o.timing = false;
    else if (a == "--huge") o.huge = true;
    else if (a == "--pin") { std::string s = val(); auto c = s.find(','); o.pin_p = std::atoi(s.c_str()); o.pin_c = c == std::string::npos ? -1 : std::atoi(s.c_str() + c + 1); }
    else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
  }
  Data d; std::vector<u8> genbuf; void* map = nullptr; std::size_t maplen = 0;
  if (!o.file.empty()) {
    int fd = open(o.file.c_str(), O_RDONLY);
    if (fd < 0) { std::perror("open"); return 1; }
    struct stat st; fstat(fd, &st);
    maplen = (std::size_t)st.st_size;
    int flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    flags |= MAP_POPULATE;
#endif
    map = mmap(nullptr, maplen, PROT_READ, flags, fd, 0);
    if (map == MAP_FAILED) { std::perror("mmap"); return 1; }
    d = {static_cast<const u8*>(map), maplen};
  } else if (o.gen) {
    FlowConfig fc; fc.n_syms = o.symbols;
    FlowGen g(fc, o.gen);
    genbuf.reserve(o.gen * 40);
    u8 tmp[64];
    for (std::size_t i = 0; i < o.gen; ++i) { Msg m = g.next(); std::size_t k = itch::encode(m, m.t0, tmp); genbuf.insert(genbuf.end(), tmp, tmp + k); }
    d = {genbuf.data(), genbuf.size()};
  } else { std::fprintf(stderr, "need --file or --gen\n"); return 2; }

  const double npt = ns_per_tick();
  const u64 ovh = timer_overhead_ticks();
  std::printf("tsc: %.4f ns/tick | timer overhead: %.1f ns | queue=%s book=%s rate=%s pin=%d,%d cap=%u huge=%d timing=%d\n",
              npt, ovh * npt, o.queue.c_str(), o.book.c_str(), o.rate ? std::to_string(o.rate).c_str() : "max",
              o.pin_p, o.pin_c, o.cap, (int)o.huge, (int)o.timing);

  std::ofstream csv;
  if (!o.csv.empty()) {
    csv.open(o.csv, std::ios::app);
    if (csv.tellp() == 0)
      csv << "queue,book,rate,run,msgs,secs,mmsgs_per_s,e2e_p50_ns,e2e_p99_ns,e2e_p999_ns,e2e_max_ns,apply_p50_ns,apply_p99_ns,apply_p999_ns,apply_max_ns,full_stalls,allocs_cons,rejected,checksum\n";
  }
  std::vector<double> rates;
  u64 first_sum = 0; bool sum_ok = true;
  for (int run = 0; run < o.runs; ++run) {
    Result r = dispatch(d, o, ovh);
    double mps = r.msgs / r.secs / 1e6;
    rates.push_back(mps);
    if (run == 0) first_sum = r.checksum; else if (r.checksum != first_sum) sum_ok = false;
    auto ns = [&](const Histogram& h, double q) { return (double)h.quantile(q) * npt; };
    std::printf("run %d: %llu msgs %.3fs  %.2f Mmsg/s | stalls=%llu allocs(cons)=%llu rejected=%llu | pin ok=%d,%d\n", run,
                (unsigned long long)r.msgs, r.secs, mps, (unsigned long long)r.stalls, (unsigned long long)r.allocs_c,
                (unsigned long long)r.rejected, (int)r.pin_p, (int)r.pin_c);
    if (o.timing)
      std::printf("        apply ns: p50=%.0f p99=%.0f p99.9=%.0f max=%.0f | e2e ns: p50=%.0f p99=%.0f p99.9=%.0f max=%.0f\n",
                  ns(r.apply, .5), ns(r.apply, .99), ns(r.apply, .999), r.apply.max() * npt,
                  ns(r.e2e, .5), ns(r.e2e, .99), ns(r.e2e, .999), r.e2e.max() * npt);
    if (csv.is_open())
      csv << o.queue << ',' << o.book << ',' << o.rate << ',' << run << ',' << r.msgs << ',' << r.secs << ',' << mps << ','
          << ns(r.e2e, .5) << ',' << ns(r.e2e, .99) << ',' << ns(r.e2e, .999) << ',' << r.e2e.max() * npt << ','
          << ns(r.apply, .5) << ',' << ns(r.apply, .99) << ',' << ns(r.apply, .999) << ',' << r.apply.max() * npt << ','
          << r.stalls << ',' << r.allocs_c << ',' << r.rejected << ',' << r.checksum << '\n';
  }
  std::sort(rates.begin(), rates.end());
  std::printf("median %.2f Mmsg/s over %d runs (min %.2f, max %.2f) | final-state checksum %s across runs\n",
              rates[rates.size() / 2], o.runs, rates.front(), rates.back(), sum_ok ? "identical" : "DIFFERS");
  if (map) munmap(map, maplen);
  return sum_ok ? 0 : 1;
}
