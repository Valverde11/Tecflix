#!/usr/bin/env python3
"""Prueba no destructiva de carga HTTP, concurrente y acotada. Solo GET.

No mide una mejora por si sola: comparar mismo endpoint, hardware y parametros,
con dos backends activos vs uno activo. Guarda resultados en JSON.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def one(url, timeout):
    start = time.perf_counter()
    status = None
    try:
        with urlopen(Request(url, headers={"Connection": "close"}), timeout=timeout) as resp:
            status = resp.status
            resp.read()
    except HTTPError as exc:
        status = exc.code
    except (TimeoutError, URLError, OSError):
        pass
    return status, (time.perf_counter() - start) * 1000


def percentile(values, pct):
    if not values:
        return None
    ordered = sorted(values)
    index = int((len(ordered) - 1) * pct)
    return round(ordered[index], 2)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--url", default="http://127.0.0.1:9282/api/metadata/videos?page=1&page_size=20")
    p.add_argument("--label", required=True, help="Ej.: un-backend o dos-backends")
    p.add_argument("--requests", type=int, default=100)
    p.add_argument("--concurrency", type=int, default=10)
    p.add_argument("--timeout", type=float, default=8)
    args = p.parse_args()
    if not (1 <= args.concurrency <= 60 and 1 <= args.requests <= 2000):
        p.error("Límites de laboratorio: 1-60 concurrentes, 1-2000 solicitudes")
    if not args.url.startswith(("http://127.0.0.1:", "http://localhost:")):
        p.error("Por seguridad, la prueba solo permite balanceadores en localhost")
    begun = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = [pool.submit(one, args.url, args.timeout) for _ in range(args.requests)]
        results = [f.result() for f in as_completed(futures)]
    elapsed = time.perf_counter() - begun
    latencies = [x[1] for x in results]
    counts = {}
    for status, _ in results:
        key = str(status) if status is not None else "error_red"
        counts[key] = counts.get(key, 0) + 1
    summary = {
        "date_utc": datetime.now(timezone.utc).isoformat(), "label": args.label,
        "url": args.url, "requests": args.requests, "concurrency": args.concurrency,
        "seconds": round(elapsed, 3), "requests_per_second": round(args.requests / elapsed, 2),
        "latency_ms_p50": percentile(latencies, .50),
        "latency_ms_p95": percentile(latencies, .95),
        "latency_ms_mean": round(statistics.mean(latencies), 2),
        "status_counts": counts,
    }
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    dest = Path("load-tests/results")
    dest.mkdir(parents=True, exist_ok=True)
    out = dest / f"v2_{args.label.replace('/', '-')}_{int(time.time())}.json"
    out.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    print(f"Reporte: {out}")
    return 0 if counts.get("error_red", 0) == 0 and sum(
        value for code, value in counts.items() if code != "200"
    ) == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
