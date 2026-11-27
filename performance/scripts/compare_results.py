#!/usr/bin/env python3
"""Compares two result folders produced by run_baseline.sh + summarize_results.py.

    python3 performance/scripts/compare_results.py performance/results/baseline-v1 \
        performance/results/after-o1 [--route /api/metadata/videos]

Prints, for every run present in both folders, the k6 numbers side by side and the
server-side average of one route (default: the catalog). Writes the same rows to
<after>/compare_vs_<before>.csv. Only p50/p95, throughput, failures and the server
average are compared: max/p99 are distorted by WSL2 clock jumps (see BASELINE_V1.md).
"""
import argparse
import csv
import os


def read_rows(path, key):
    if not os.path.exists(path):
        return {}
    with open(path, newline="") as handle:
        return {key(row): row for row in csv.DictReader(handle)}


def change(before, after):
    """Relative change after vs before, as text ("-93.1%"); empty when not computable."""
    try:
        b, a = float(before), float(after)
    except (TypeError, ValueError):
        return ""
    if b == 0:
        return ""
    return f"{(a - b) / b * 100:+.1f}%"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--route", default="/api/metadata/videos")
    args = parser.parse_args()

    k6_key = lambda row: row["run"]
    before_k6 = read_rows(os.path.join(args.before, "summary_k6.csv"), k6_key)
    after_k6 = read_rows(os.path.join(args.after, "summary_k6.csv"), k6_key)
    server_key = lambda row: (row["run"], row["method"], row["route"])
    before_srv = read_rows(os.path.join(args.before, "summary_server.csv"), server_key)
    after_srv = read_rows(os.path.join(args.after, "summary_server.csv"), server_key)

    header = ["run", "req_s_before", "req_s_after", "req_s_change",
              "p50_before", "p50_after", "p95_before", "p95_after", "p95_change",
              "failed_before", "failed_after",
              "route_avg_before", "route_avg_after", "route_avg_change"]
    rows = []
    for run in after_k6:  # keeps the execution order of the "after" folder
        if run not in before_k6:
            continue
        b, a = before_k6[run], after_k6[run]
        sb = before_srv.get((run, "GET", args.route), {})
        sa = after_srv.get((run, "GET", args.route), {})
        rows.append([run, b["req_per_s"], a["req_per_s"], change(b["req_per_s"], a["req_per_s"]),
                     b["p50_ms"], a["p50_ms"], b["p95_ms"], a["p95_ms"],
                     change(b["p95_ms"], a["p95_ms"]),
                     b["failed_pct"], a["failed_pct"],
                     sb.get("avg_ms", ""), sa.get("avg_ms", ""),
                     change(sb.get("avg_ms"), sa.get("avg_ms"))])

    out = os.path.join(args.after, f"compare_vs_{os.path.basename(os.path.normpath(args.before))}.csv")
    with open(out, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(header)
        writer.writerows(rows)

    widths = [max(len(str(x)) for x in col) for col in zip(header, *rows)] if rows else []
    for line in [header] + rows:
        print("  ".join(str(x).rjust(w) for x, w in zip(line, widths)))
    print(f"\nServer route: GET {args.route}   ->  {out}")


if __name__ == "__main__":
    main()
