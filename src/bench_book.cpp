#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "ll/alloc_counter.hpp"
#include "ll/apply.hpp"
#include "ll/book_map.hpp"
#include "ll/flowgen.hpp"
#include "ll/histogram.hpp"
using namespace ll;

struct Row { std::string name; double mps, p50, p90, p99, p999, mx, allocs_per_msg; };

template <class B>
static Row bench(const char* name, const std::vector<Msg>& msgs, u32 syms, int runs, u64 ovh, u32 max_orders) {
  BookConfig bc; bc.n_symbols = syms; bc.max_orders = max_orders;
  const double npt = ns_per_tick();
  std::vector<double> mps; double allocs = 0;
  for (int r = 0; r < runs; ++r) {
    B b(bc);
    u64 a0 = thread_alloc_count();
    u64 t0 = steady_ns();
    for (const Msg& m : msgs) apply(b, m);
    u64 t1 = steady_ns();
    allocs = double(thread_alloc_count() - a0) / double(msgs.size());
    mps.push_back(msgs.size() / ((t1 - t0) / 1e3));
  }
  std::sort(mps.begin(), mps.end());
  Histogram h;
  { B b(bc);
    for (const Msg& m : msgs) {
      u64 a = tsc_now();
      apply(b, m);
      u64 c = tsc_now();
      u64 dt = c - a;
      h.record(dt > ovh ? dt - ovh : 0);
    } }
  return {name, mps[mps.size() / 2], h.quantile(.5) * npt, h.quantile(.9) * npt, h.quantile(.99) * npt,
          h.quantile(.999) * npt, h.max() * npt, allocs};
}

int main(int argc, char** argv) {
  std::size_t n = 4'000'000; int runs = 3; std::string csv; int pin = -1; int pos = 0; u32 max_orders = 1u << 18;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--csv" && i + 1 < argc) csv = argv[++i];
    else if (a == "--pin" && i + 1 < argc) pin = std::atoi(argv[++i]);
    else if (a == "--max-orders" && i + 1 < argc) max_orders = (u32)std::strtoul(argv[++i], nullptr, 10);
    else if (pos == 0) { n = std::strtoull(argv[i], nullptr, 10); ++pos; }
    else if (pos == 1) { runs = std::atoi(argv[i]); ++pos; }
  }
  bool pinned = pin_thread(pin);
  FlowConfig fc;
  FlowGen g(fc, n);
  std::vector<Msg> msgs(n);
  for (auto& m : msgs) m = g.next();
  const u64 ovh = timer_overhead_ticks();
  std::printf("max_orders=%u | messages=%zu runs=%d | tsc %.4f ns/tick | timer overhead %.1f ns (subtracted) | pinned=%d\n", max_orders, n, runs, ns_per_tick(),
              ovh * ns_per_tick(), (int)pinned);
  std::vector<Row> rows;
  rows.push_back(bench<BookMap>("map+list+unordered_map (baseline)", msgs, fc.n_syms, runs, ovh, max_orders));
  rows.push_back(bench<BookLadderStdHash>("ladder+bitmap, std::unordered_map ids", msgs, fc.n_syms, runs, ovh, max_orders));
  rows.push_back(bench<BookLadder>("ladder+bitmap, flat-hash ids", msgs, fc.n_syms, runs, ovh, max_orders));
  std::printf("\n%-40s %9s | %7s %7s %7s %8s %9s | %s\n", "book", "Mmsg/s", "p50 ns", "p90", "p99", "p99.9", "max", "allocs/msg");
  for (auto& r : rows)
    std::printf("%-40s %9.2f | %7.0f %7.0f %7.0f %8.0f %9.0f | %.3f\n", r.name.c_str(), r.mps, r.p50, r.p90, r.p99, r.p999, r.mx, r.allocs_per_msg);
  if (!csv.empty()) {
    std::ofstream f(csv);
    f << "book,mmsgs_per_s,p50_ns,p90_ns,p99_ns,p999_ns,max_ns,allocs_per_msg\n";
    for (auto& r : rows) f << '"' << r.name << "\"," << r.mps << ',' << r.p50 << ',' << r.p90 << ',' << r.p99 << ',' << r.p999 << ',' << r.mx << ',' << r.allocs_per_msg << '\n';
  }
}
