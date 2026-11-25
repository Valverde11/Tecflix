#!/usr/bin/env python3
import argparse
import csv
import json
import re
import sys
from pathlib import Path

SUMMARY_PATTERN = re.compile(r"^(single|dual)-(\d+)vus-summary\.json$")
CSV_FIELDS = [
    "vus",
    "single_p95_ms",
    "dual_p95_ms",
    "p95_change_percent",
    "single_requests_per_second",
    "dual_requests_per_second",
    "requests_change_percent",
    "single_failed_percent",
    "dual_failed_percent",
    "single_checks_passed_percent",
    "dual_checks_passed_percent",
]


def read_metrics(path):
    try:
        summary = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"No se pudo leer el resumen {path}: {error}") from error

    if not isinstance(summary, dict):
        raise ValueError(f"El resumen {path} debe ser un objeto JSON")
    metrics = summary.get("metrics", {})
    if not isinstance(metrics, dict):
        raise ValueError(f"El resumen {path} no contiene un objeto metrics")

    def value(metric_name, field):
        try:
            return float(metrics[metric_name]["values"][field])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(
                f"El resumen {path} no contiene {metric_name}.values.{field}"
            ) from error

    return {
        "p95_ms": value("http_req_duration", "p(95)"),
        "requests_per_second": value("http_reqs", "rate"),
        "failed_percent": value("http_req_failed", "rate") * 100,
        "checks_passed_percent": value("checks", "rate") * 100,
    }


def percent_change(baseline, comparison):
    if baseline == 0:
        return None
    return ((comparison - baseline) / baseline) * 100


def build_row(vus, single, dual):
    p95_change = percent_change(single["p95_ms"], dual["p95_ms"])
    requests_change = percent_change(
        single["requests_per_second"], dual["requests_per_second"]
    )
    return {
        "vus": vus,
        "single_p95_ms": single["p95_ms"],
        "dual_p95_ms": dual["p95_ms"],
        "p95_change_percent": p95_change,
        "single_requests_per_second": single["requests_per_second"],
        "dual_requests_per_second": dual["requests_per_second"],
        "requests_change_percent": requests_change,
        "single_failed_percent": single["failed_percent"],
        "dual_failed_percent": dual["failed_percent"],
        "single_checks_passed_percent": single["checks_passed_percent"],
        "dual_checks_passed_percent": dual["checks_passed_percent"],
    }


def collect_pairs(results_dir):
    summaries = {}
    for path in results_dir.glob("*-summary.json"):
        match = SUMMARY_PATTERN.fullmatch(path.name)
        if match:
            topology, vus = match.groups()
            summaries.setdefault(int(vus), {})[topology] = path

    if not summaries:
        raise ValueError(f"No se encontraron resúmenes de k6 en {results_dir}")

    rows = []
    for vus, pair in sorted(summaries.items()):
        if set(pair) != {"single", "dual"}:
            raise ValueError(f"Falta el resumen de una topología para {vus} VUs")
        rows.append(
            build_row(
                vus,
                read_metrics(pair["single"]),
                read_metrics(pair["dual"]),
            )
        )
    return rows


def write_report(results_dir, rows):
    output = results_dir / "comparison.csv"
    with output.open("w", encoding="utf-8", newline="") as report:
        writer = csv.DictWriter(report, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print("VUs | p95 single/dual (ms) | req/s single/dual | errores single/dual (%)")
    for row in rows:
        print(
            f"{row['vus']:>3} | "
            f"{row['single_p95_ms']:.2f}/{row['dual_p95_ms']:.2f} | "
            f"{row['single_requests_per_second']:.2f}/"
            f"{row['dual_requests_per_second']:.2f} | "
            f"{row['single_failed_percent']:.2f}/"
            f"{row['dual_failed_percent']:.2f}"
        )
    print(f"Comparación CSV: {output}")


def main():
    parser = argparse.ArgumentParser(
        description="Compara resúmenes k6 pareados de Tecflix."
    )
    parser.add_argument("results_dir", type=Path)
    args = parser.parse_args()

    try:
        rows = collect_pairs(args.results_dir)
        write_report(args.results_dir, rows)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
