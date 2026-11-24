#pragma once
#include <cstdio>
#include <cstdlib>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define CHECK_MSG(c, ...) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s | ", __FILE__, __LINE__, #c); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); ++g_fail; } } while (0)
#define DONE(name) do { std::printf("%s: %s\n", name, g_fail ? "FAILED" : "ok"); return g_fail ? 1 : 0; } while (0)
