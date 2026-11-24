#include <vector>
#include "check.hpp"
#include "ll/flowgen.hpp"
using namespace ll;

int main() {
  const std::size_t N = 200000;
  FlowGen g(FlowConfig{}, N);
  std::vector<Msg> orig(N);
  std::vector<u8> buf(N * 40);
  std::size_t off = 0;
  for (std::size_t i = 0; i < N; ++i) {
    orig[i] = g.next();
    off += itch::encode(orig[i], orig[i].t0, buf.data() + off);
  }
  itch::FrameReader fr{buf.data(), buf.data() + off};
  const u8* body; std::size_t len; std::size_t i = 0;
  int counts[256] = {};
  while (fr.next(body, len)) {
    Msg m{};
    CHECK(itch::parse_msg(body, len, m));
    const Msg& o = orig[i];
    CHECK(m.type == o.type); CHECK(m.locate == o.locate); CHECK(m.ref == o.ref);
    switch (o.type) {
      case 'A': CHECK(m.side == o.side); CHECK(m.qty == o.qty); CHECK(m.price == o.price); break;
      case 'E': case 'X': CHECK(m.qty == o.qty); break;
      case 'U': CHECK(m.ref2 == o.ref2); CHECK(m.qty == o.qty); CHECK(m.price == o.price); break;
      default: break;
    }
    ++counts[m.type]; ++i;
    if (g_fail > 10) break;
  }
  CHECK(i == N);

  u8 junk[8] = {'Z', 0, 0, 0, 0, 0, 0, 0}; Msg m{};
  CHECK(!itch::parse_msg(junk, 8, m));
  u8 shortA[10] = {'A'}; CHECK(!itch::parse_msg(shortA, 10, m));
  std::printf("mix: A=%d D=%d E=%d X=%d U=%d\n", counts['A'], counts['D'], counts['E'], counts['X'], counts['U']);
  DONE("test_parser");
}
