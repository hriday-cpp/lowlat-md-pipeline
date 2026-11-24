#include <unordered_map>
#include "check.hpp"
#include "ll/id_map.hpp"
using namespace ll;

int main() {
  const u32 cap = 1 << 12;
  FlatIdMap f(cap, false);
  std::unordered_map<u64, u32> ref;
  Rng rng(99);
  for (int i = 0; i < 3'000'000; ++i) {
    u64 k = 1 + rng.below(6000);
    u32 op = rng.below(3);
    if (op == 0 && ref.size() < cap) {
      u32 v = rng.next32() & 0x7FFFFFFF;
      bool ins = ref.emplace(k, v).second;
      CHECK(f.insert(k, v) == ins);
    } else if (op == 1) {
      f.erase(k); ref.erase(k);
    } else {
      auto it = ref.find(k);
      CHECK(f.find(k) == (it == ref.end() ? kNil : it->second));
    }
    if (g_fail > 10) break;
  }
  for (u64 k = 1; k <= 6000; ++k) { auto it = ref.find(k); CHECK(f.find(k) == (it == ref.end() ? kNil : it->second)); }
  DONE("test_hash");
}
