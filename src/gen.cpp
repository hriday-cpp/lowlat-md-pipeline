#include <cstdio>
#include <cstdlib>
#include <vector>
#include "ll/flowgen.hpp"
using namespace ll;

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: gen <out.itch> [n_msgs] [seed] [symbols]\n"); return 2; }
  std::size_t n = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 4'000'000;
  FlowConfig fc;
  fc.seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
  fc.n_syms = argc > 4 ? (u32)std::strtoul(argv[4], nullptr, 10) : 4;
  FlowGen g(fc, n);
  std::vector<u8> buf;
  buf.reserve(n * 40);
  u8 tmp[64];
  for (std::size_t i = 0; i < n; ++i) {
    Msg m = g.next();
    std::size_t k = itch::encode(m, m.t0, tmp);
    buf.insert(buf.end(), tmp, tmp + k);
  }
  FILE* f = std::fopen(argv[1], "wb");
  if (!f) { std::perror("fopen"); return 1; }
  std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  std::printf("wrote %zu messages, %.1f MB -> %s\n", n, buf.size() / 1e6, argv[1]);
}
