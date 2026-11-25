import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1]))
from scripts.report import build_row, collect_pairs, percent_change, write_report


class ReportTests(unittest.TestCase):
    def test_computes_changes_against_single_instance(self):
        row = build_row(
            10,
            {
                "p95_ms": 100,
                "requests_per_second": 20,
                "failed_percent": 1,
                "checks_passed_percent": 99,
            },
            {
                "p95_ms": 80,
                "requests_per_second": 30,
                "failed_percent": 0.5,
                "checks_passed_percent": 99.5,
            },
        )

        self.assertEqual(row["p95_change_percent"], -20)
        self.assertEqual(row["requests_change_percent"], 50)
        self.assertEqual(row["single_failed_percent"], 1)
        self.assertEqual(row["dual_checks_passed_percent"], 99.5)

    def test_reports_undefined_change_for_zero_baseline(self):
        self.assertIsNone(percent_change(0, 10))

    def test_reads_paired_k6_summaries(self):
        metrics = {
            "http_req_duration": {"values": {"p(95)": 100}},
            "http_reqs": {"values": {"rate": 12}},
            "http_req_failed": {"values": {"rate": 0.01}},
            "checks": {"values": {"rate": 0.99}},
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            results_dir = Path(temporary_directory)
            for topology in ("single", "dual"):
                summary = results_dir / f"{topology}-5vus-summary.json"
                summary.write_text(
                    json.dumps({"metrics": metrics}),
                    encoding="utf-8",
                )

            rows = collect_pairs(results_dir)

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["vus"], 5)
        self.assertEqual(rows[0]["single_p95_ms"], 100)
        self.assertEqual(rows[0]["dual_requests_per_second"], 12)

    def test_writes_comparison_csv(self):
        row = build_row(
            5,
            {
                "p95_ms": 100,
                "requests_per_second": 10,
                "failed_percent": 1,
                "checks_passed_percent": 99,
            },
            {
                "p95_ms": 80,
                "requests_per_second": 12,
                "failed_percent": 0,
                "checks_passed_percent": 100,
            },
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            results_dir = Path(temporary_directory)
            write_report(results_dir, [row])
            with (results_dir / "comparison.csv").open(
                encoding="utf-8", newline=""
            ) as report:
                written = next(csv.DictReader(report))

        self.assertEqual(written["vus"], "5")
        self.assertEqual(written["p95_change_percent"], "-20.0")


if __name__ == "__main__":
    unittest.main()
