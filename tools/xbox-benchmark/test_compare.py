# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import io
import tempfile
import unittest
from pathlib import Path

import compare


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "run.csv"
        self.manifest = {
            "suite": compare.SUITE,
            "xbe_crc32": "12345678",
            "xbe_bytes": 100,
        }

    def fixture(self, mutate=None):
        f = io.StringIO()
        f.write(
            f"# {compare.SUITE},0000000000000001,"
            "hardware-unverified,12345678,100,733333333\n"
        )
        w = csv.writer(f, lineterminator="\n")
        w.writerow(compare.FIELDS)
        seq = 0
        for trial in range(-1, compare.TRIALS):
            for cid, spec in enumerate(compare.cases()):
                seq += 1
                ticks = 733 if cid == 0 else 73333333
                r = dict(
                    zip(
                        compare.FIELDS,
                        [
                            seq,
                            cid,
                            trial,
                            *spec,
                            100,
                            100 + ticks,
                            ticks,
                            0 if cid == 0 else 100,
                            compare.expected(spec),
                            1,
                        ],
                    )
                )
                if mutate:
                    mutate(r)
                w.writerow(r[k] for k in compare.FIELDS)
        f.write("# complete\n")
        self.path.write_text(f.getvalue())
        return compare.read_run(
            self.path, self.manifest, "hardware-unverified"
        )[1]

    def test_complete_34_cases_and_272_intervals(self):
        rows = self.fixture()
        self.assertEqual(len(rows), 272)
        self.assertEqual(len(compare.cases()), 34)

    def test_reject_corrupt_output(self):
        with self.assertRaisesRegex(ValueError, "verification"):
            self.fixture(lambda r: r.update(checksum=0xFFFFFFFF))

    def test_reject_workload_change(self):
        with self.assertRaisesRegex(ValueError, "Workload"):
            self.fixture(lambda r: r.update(iterations=r["iterations"] + 1))

    def test_reject_duplicate_and_missing_sequence(self):
        with self.assertRaisesRegex(ValueError, "sequence"):
            self.fixture(lambda r: r.update(seq=1))

    def test_reject_bad_tsc(self):
        with self.assertRaisesRegex(ValueError, "TSC"):
            self.fixture(lambda r: r.update(tsc=0))

    def test_reject_missing_completion(self):
        self.fixture()
        self.path.write_text(self.path.read_text().replace("# complete\n", ""))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            compare.read_run(self.path, self.manifest, "hardware-unverified")

    def test_reject_other_binary(self):
        self.fixture()
        self.manifest["xbe_crc32"] = "00000000"
        with self.assertRaisesRegex(ValueError, "Executable"):
            compare.read_run(self.path, self.manifest, "hardware-unverified")

    def test_reject_wrong_role(self):
        self.fixture()
        with self.assertRaisesRegex(ValueError, "role"):
            compare.read_run(self.path, self.manifest, "xemu")

    def test_host_requires_session_complete_positive_unique(self):
        for text in (
            "other,1,100\n",
            "0000000000000001,1,-1\n",
            "0000000000000001,1,100\n" * 2,
            "",
        ):
            self.path.write_text(text)
            with self.assertRaises(ValueError):
                compare.read_host(self.path, "0000000000000001", 1)
        self.path.write_text("0000000000000001,1,100\n")
        self.assertEqual(
            compare.read_host(self.path, "0000000000000001", 1), {1: 1e-7}
        )

    def test_formula_uses_host_not_emulated_tsc(self):
        rows = self.fixture()
        host = {r["seq"]: r["seconds"] * 1.25 for r in rows}
        report = compare.summarize(rows, rows, host)
        self.assertIsNone(report["gameplay_speed"])
        self.assertAlmostEqual(report["cases"][0]["speed_percent"], 80)
        self.assertEqual(len(report["cases"]), 33)

    def test_short_or_noisy_results_are_unavailable(self):
        rows = self.fixture()
        host = {r["seq"]: 0.001 for r in rows}
        self.assertIsNone(
            compare.summarize(rows, rows, host)["cases"][0]["speed_percent"]
        )
        host = {r["seq"]: r["seconds"] for r in rows}
        host[
            next(r["seq"] for r in rows if r["case"] == 1 and r["trial"] == 0)
        ] *= 2
        self.assertIsNone(
            compare.summarize(rows, rows, host)["cases"][0]["speed_percent"]
        )

    def test_reject_unverified_provenance(self):
        with self.assertRaises(ValueError):
            compare.validate_metadata({}, {})


if __name__ == "__main__":
    unittest.main()
