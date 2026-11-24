#!/usr/bin/env python3
import csv, os, statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def rows(p):
    return list(csv.DictReader(open(p))) if os.path.exists(p) else []

out = []
spsc = rows("results/spsc.csv")
if spsc:
    names = [r["queue"] for r in spsc]; thr = [float(r["mmsgs_per_s_median"]) for r in spsc]
    fig, ax = plt.subplots(figsize=(6, 3.2)); ax.bar(names, thr, color="#534AB7")
    ax.set_ylabel("Mmsg/s (median)"); ax.set_title("SPSC queue throughput (64 B messages)")
    for i, v in enumerate(thr): ax.text(i, v, f"{v:.1f}", ha="center", va="bottom", fontsize=8)
    fig.tight_layout(); fig.savefig("docs/spsc_throughput.png", dpi=140); plt.close(fig)
    out += ["### SPSC queues", "", "| queue | Mmsg/s (median) | one-way p50 ns | p99 ns | p99.9 ns |", "|---|---|---|---|---|"]
    for r in spsc:
        out.append(f"| {r['queue']} | {float(r['mmsgs_per_s_median']):.2f} | {r.get('oneway_p50_ns') or '-'} | {r.get('oneway_p99_ns') or '-'} | {r.get('oneway_p999_ns') or '-'} |")
    out.append("")

book = rows("results/book.csv")
if book:
    names = [r["book"].replace(" (baseline)", "").replace(", ", "\n") for r in book]
    fig, axs = plt.subplots(1, 2, figsize=(10, 3.4))
    axs[0].bar(range(len(book)), [float(r["mmsgs_per_s"]) for r in book], color="#0F6E56"); axs[0].set_title("Throughput (Mmsg/s)")
    w = 0.25
    for k, (key, lab) in enumerate([("p50_ns", "p50"), ("p99_ns", "p99"), ("p999_ns", "p99.9")]):
        axs[1].bar([i + k * w for i in range(len(book))], [float(r[key]) for r in book], w, label=lab)
    axs[1].set_title("Per-op apply latency (ns)"); axs[1].legend()
    for a in axs: a.set_xticks([i + (w if a is axs[1] else 0) for i in range(len(book))]); a.set_xticklabels(names, fontsize=7, rotation=12)
    fig.tight_layout(); fig.savefig("docs/book_compare.png", dpi=140); plt.close(fig)
    out += ["### Order book implementations (single thread)", "", "| book | Mmsg/s | p50 ns | p90 | p99 | p99.9 | max | allocs/msg |", "|---|---|---|---|---|---|---|---|"]
    for r in book:
        out.append(f"| {r['book']} | {float(r['mmsgs_per_s']):.2f} | {float(r['p50_ns']):.0f} | {float(r['p90_ns']):.0f} | {float(r['p99_ns']):.0f} | {float(r['p999_ns']):.0f} | {float(r['max_ns']):.0f} | {float(r['allocs_per_msg']):.3f} |")
    out.append("")

def med(rs, k): return st.median(float(r[k]) for r in rs)
for f, title in [("replay_throughput", "Pipeline max throughput (no per-message timing)"), ("replay_paced", "Pipeline end-to-end, paced"),
                 ("replay_nopin", "Paced, unpinned"), ("replay_huge", "Paced, pinned + hugepages")]:
    rs = rows(f"results/{f}.csv")
    if not rs: continue
    out += [f"### {title}", "", "| queue | book | Mmsg/s | e2e p50 ns | p99 | p99.9 | apply p50 | p99 | p99.9 | allocs |", "|---|---|---|---|---|---|---|---|---|---|"]
    keys = sorted({(r["queue"], r["book"]) for r in rs}, key=lambda x: [r["queue"] + r["book"] for r in rs].index(x[0] + x[1]))
    for q, b in keys:
        g = [r for r in rs if r["queue"] == q and r["book"] == b]
        out.append(f"| {q} | {b} | {med(g,'mmsgs_per_s'):.2f} | {med(g,'e2e_p50_ns'):.0f} | {med(g,'e2e_p99_ns'):.0f} | {med(g,'e2e_p999_ns'):.0f} | {med(g,'apply_p50_ns'):.0f} | {med(g,'apply_p99_ns'):.0f} | {med(g,'apply_p999_ns'):.0f} | {g[0]['allocs_cons']} |")
    out.append("")
sysinfo = open("results/system.txt").read() if os.path.exists("results/system.txt") else ""
open("results/summary.md", "w").write("```\n" + sysinfo + "```\n\n" + "\n".join(out))
print("wrote results/summary.md")
