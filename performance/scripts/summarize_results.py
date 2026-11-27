#!/usr/bin/env python3
"""Builds CSV tables from a run_baseline.sh results folder.

For each run in runs.csv it combines:
  * k6 client-side numbers (throughput, p50/p95/p99, failure rate) from <run>.json
  * server-side numbers from Prometheus (OpenTelemetry histograms) per service/route
  * average/max CPU and RSS of each API process from process_samples.csv

Outputs (in the same folder): summary_k6.csv, summary_server.csv, summary_process.csv

  python3 performance/scripts/summarize_results.py performance/results/baseline-v1
"""
import csv
import json
import sys
import urllib.parse
import urllib.request
from collections import defaultdict
from pathlib import Path

PROM = "http://127.0.0.1:9090"
SCRAPE_SLACK = 20  # seconds; scrape interval is 15 s


def prom(query, at):
    url = f"{PROM}/api/v1/query?" + urllib.parse.urlencode({"query": query, "time": at})
    with urllib.request.urlopen(url, timeout=30) as response:
        return json.load(response)["data"]["result"]


def k6_row(run, path):
    data = json.loads(path.read_text())
    m = data["metrics"]
    dur = m.get("http_req_duration", {})
    reqs = m.get("http_reqs", {})
    failed = m.get("http_req_failed", {})
    return {
        "run": run["run"], "scenario": run["scenario"], "vus": run["vus"], "think": run["think"],
        "requests": reqs.get("count", 0),
        "req_per_s": round(reqs.get("rate", 0), 1),
        "p50_ms": round(dur.get("med", 0), 2),
        "p95_ms": round(dur.get("p(95)", 0), 2),
        "p99_ms": round(dur.get("p(99)", 0), 2),
        "max_ms": round(dur.get("max", 0), 2),
        "failed_pct": round(100 * failed.get("value", 0), 3),
    }


def server_rows(run):
    start, end = int(run["start_epoch"]), int(run["end_epoch"])
    window = f"{end - start + SCRAPE_SLACK}s"
    at = end + SCRAPE_SLACK
    by = "service_name, http_route, http_request_method"
    base = "http_server_request_duration_seconds"
    rows = defaultdict(dict)

    def put(result, key):
        for item in result:
            lab = item["metric"]
            if lab.get("http_route") == "/health":
                continue
            ident = (lab.get("service_name"), lab.get("http_route"), lab.get("http_request_method"))
            value = float(item["value"][1])
            if value == value:  # skip NaN
                rows[ident][key] = value

    put(prom(f"sum by ({by}) (increase({base}_count[{window}]))", at), "count")
    put(prom(f"sum by ({by}) (increase({base}_sum[{window}])) / "
             f"sum by ({by}) (increase({base}_count[{window}]))", at), "avg_s")
    for q in (0.5, 0.95, 0.99):
        put(prom(f"histogram_quantile({q}, sum by (le, {by}) "
                 f"(increase({base}_bucket[{window}])))", at), f"p{int(q * 100)}_s")
    put(prom(f"sum by ({by}) (increase({base}_count{{http_response_status_code=~\"5..\"}}[{window}]))", at),
        "errors_5xx")

    out = []
    for (service, route, method), values in sorted(rows.items()):
        if values.get("count", 0) < 1:
            continue
        out.append({
            "run": run["run"], "service": service, "method": method, "route": route,
            "requests": round(values.get("count", 0)),
            "avg_ms": round(1000 * values.get("avg_s", 0), 2),
            "p50_ms": round(1000 * values.get("p50_s", 0), 2),
            "p95_ms": round(1000 * values.get("p95_s", 0), 2),
            "p99_ms": round(1000 * values.get("p99_s", 0), 2),
            "errors_5xx": round(values.get("errors_5xx", 0)),
        })
    return out


def process_rows(folder):
    """Average CPU (100 % = one core) and peak RSS of each API process per run.

    Newer samples carry cpu_seconds (utime+stime from /proc). Older samples only
    have ps %cpu, which is a lifetime average; in that case the cumulative CPU
    time is rebuilt as pct * (epoch - process start) using process_start.csv.
    """
    path = folder / "process_samples.csv"
    if not path.exists():
        return []
    starts = {}
    start_path = folder / "process_start.csv"
    if start_path.exists():
        with start_path.open() as f:
            starts = {row["pid"]: int(row["start_epoch"]) for row in csv.DictReader(f)}
    acc = defaultdict(list)
    with path.open() as f:
        for row in csv.DictReader(f):
            epoch = int(row["epoch"])
            if row.get("cpu_seconds"):
                cpu_seconds = float(row["cpu_seconds"])
            elif row["pid"] in starts:
                cpu_seconds = float(row["cpu_percent"]) / 100 * (epoch - starts[row["pid"]])
            else:
                continue
            acc[(row["run"], row["service"])].append((epoch, cpu_seconds, int(row["rss_kb"])))
    out = []
    for (run, service), samples in acc.items():
        samples.sort()
        (t0, c0, _), (t1, c1, _) = samples[0], samples[-1]
        cpu = 100 * (c1 - c0) / (t1 - t0) if t1 > t0 else 0.0
        out.append({
            "run": run, "service": service,
            "cpu_avg_pct": round(max(cpu, 0.0), 1),
            "rss_max_mb": round(max(s[2] for s in samples) / 1024, 1),
        })
    return out


def write(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"escrito {path} ({len(rows)} filas)")


def main():
    folder = Path(sys.argv[1])
    with (folder / "runs.csv").open() as f:
        runs = list(csv.DictReader(f))
    k6 = [k6_row(r, folder / f"{r['run']}.json") for r in runs if (folder / f"{r['run']}.json").exists()]
    server = [row for r in runs for row in server_rows(r)]
    write(folder / "summary_k6.csv", k6)
    write(folder / "summary_server.csv", server)
    write(folder / "summary_process.csv", process_rows(folder))


if __name__ == "__main__":
    main()
