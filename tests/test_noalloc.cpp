#include <vector>
#include "check.hpp"
#include "ll/alloc_counter.hpp"
#include "ll/apply.hpp"
#include "ll/book_map.hpp"
#include "ll/flowgen.hpp"
using namespace ll;

int main() {
  const std::size_t N = 1'000'000;
  FlowConfig fc; fc.target_live = 50000;
  FlowGen g(fc, N);
  std::vector<Msg> msgs(N);
  for (auto& m : msgs) m = g.next();
  BookConfig bc; bc.max_orders = 1 << 18; bc.n_symbols = fc.n_syms;

  { BookLadder b(bc);
    u64 a0 = thread_alloc_count();
    for (const Msg& m : msgs) apply(b, m);
    u64 d = thread_alloc_count() - a0;
    std::printf("BookLadder: %llu allocations over %zu messages\n", (unsigned long long)d, N);
    CHECK_MSG(d == 0, "hot path allocated %llu times", (unsigned long long)d); }
  { BookLadderStdHash b(bc);
    u64 a0 = thread_alloc_count();
    for (const Msg& m : msgs) apply(b, m);
    std::printf("BookLadderStdHash: %llu allocations\n", (unsigned long long)(thread_alloc_count() - a0)); }
  { BookMap b(bc);
    u64 a0 = thread_alloc_count();
    for (const Msg& m : msgs) apply(b, m);
    u64 d = thread_alloc_count() - a0;
    std::printf("BookMap (baseline): %llu allocations (%.2f per message)\n", (unsigned long long)d, double(d) / N);
    CHECK_MSG(d > N / 4, "counter sanity: baseline should allocate"); }
  DONE("test_noalloc");
}
