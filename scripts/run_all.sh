#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
PROD=${PROD:-2}; CONS=${CONS:-3}; N=${N:-4000000}; RATE=${RATE:-1000000}; RUNS=${RUNS:-5}
make -s all
mkdir -p results docs
rm -f results/*.csv

{ echo "date: $(date -u +%FT%TZ)"; echo "kernel: $(uname -sr)"; echo "cpu: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- || uname -m)"
  echo "governor: $(cat /sys/devices/system/cpu/cpu${PROD}/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  echo "thp: $(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo n/a)"
  echo "compiler: $(${CXX:-g++} --version | head -1)"; } > results/system.txt

./build/gen results/sample.itch "$N" 1 4

echo "== book (single thread) =="; ./build/bench_book "$N" "$RUNS" --pin "$CONS" --csv results/book.csv
echo "== spsc queues ==";          ./build/bench_spsc 20000000 "$RUNS" 500000 --pin "$PROD,$CONS" --csv results/spsc.csv

echo "== pipeline: max throughput (no per-message timing) =="
for q in mutex naive padded cached; do
  ./build/replay --file results/sample.itch --queue $q --book ladder --no-timing --runs "$RUNS" --pin "$PROD,$CONS" --csv results/replay_throughput.csv | tail -1
done
for b in map stdhash ladder; do
  ./build/replay --file results/sample.itch --queue cached --book $b --no-timing --runs "$RUNS" --pin "$PROD,$CONS" --csv results/replay_throughput.csv | tail -1
done

echo "== pipeline: paced at $RATE msg/s (end-to-end latency is meaningful here) =="
for b in map ladder; do
  ./build/replay --file results/sample.itch --queue cached --book $b --rate "$RATE" --runs "$RUNS" --pin "$PROD,$CONS" --csv results/replay_paced.csv | tail -3
done

echo "== ablation: pinning and hugepages (paced) =="
./build/replay --file results/sample.itch --rate "$RATE" --runs "$RUNS" --csv results/replay_nopin.csv | tail -3
./build/replay --file results/sample.itch --rate "$RATE" --runs "$RUNS" --pin "$PROD,$CONS" --huge --csv results/replay_huge.csv | tail -3

if command -v perf >/dev/null 2>&1; then
  echo "== perf stat (book benchmark) =="
  perf stat -e cycles,instructions,cache-misses,branch-misses,L1-dcache-load-misses \
    ./build/bench_book "$N" 1 --pin "$CONS" 2> results/perf_book.txt >/dev/null || true
  cat results/perf_book.txt
else echo "perf not found: skipping perf stat (Linux: apt install linux-tools-generic)"; fi

python3 scripts/plot.py || echo "(plots skipped: pip install matplotlib pandas)"
echo "done -> results/*.csv, results/summary.md, docs/*.png"
