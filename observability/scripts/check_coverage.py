#!/usr/bin/env python3
"""Fail when LLVM coverage falls below a minimum percentage."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="Path to llvm-cov export JSON")
    parser.add_argument("--minimum", type=float, default=90.0,
                        help="Minimum percentage required for lines, functions and regions")
    args = parser.parse_args()

    try:
        payload = json.loads(args.report.read_text(encoding="utf-8"))
        totals = payload["data"][0]["totals"]
        percentages = {
            name: float(totals[name]["percent"])
            for name in ("lines", "functions", "regions")
        }
        branches = totals.get("branches")
        if isinstance(branches, dict) and int(branches.get("count", 0)) > 0:
            percentages["branches"] = float(branches["percent"])
    except (OSError, json.JSONDecodeError, KeyError, IndexError, TypeError, ValueError) as exc:
        print(f"No se pudo leer un reporte LLVM válido: {exc}", file=sys.stderr)
        return 2

    for metric, percentage in percentages.items():
        state = "OK" if percentage >= args.minimum else "FALLO"
        print(f"{state}: cobertura de {metric}: {percentage:.2f}% (mínimo {args.minimum:.2f}%)")

    return 0 if all(value >= args.minimum for value in percentages.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
