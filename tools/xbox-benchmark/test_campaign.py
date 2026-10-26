# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import io
import json
from pathlib import Path
import unittest

import compare_campaign as campaign
import test_compare


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.base = test_compare.ComparisonTests()
        self.base.setUp()
        self.addCleanup(self.base.doCleanups)
        self.base.fixture()
        self.path = self.base.path
        self.manifest = self.base.manifest
        self.catalog = [{"id": f"cpu/c{i}", "cpu": True} for i in range(11)]
        self.catalog[0]["id"] = "cpu/TestFpuBasicArithmetic"
        self.catalog += [{"id": f"nv2a/c{i}", "cpu": False} for i in range(19)]
        self.manifest["upstream_catalog"] = self.catalog
        self.header = self.path.read_text().splitlines()[0]
        self.validation = {
            "schema_version": 1,
            "suite": "xbox-benchmark-pilot-2-validation|" + self.header,
            "events": [],
            "tests": [
                {"name": c["id"], "result": "pass", "events": []}
                for c in self.catalog
            ],
            "summary": {"total": 30, "passed": 30, "failed": 0},
        }

    def fixture(self, mutate=None):
        out = io.StringIO()
        out.write(self.header + "\n")
        writer = csv.writer(out, lineterminator="\n")
        writer.writerow(campaign.FIELDS)
        seq = 272
        for cid, spec in campaign.specs(self.catalog).items():
            for trial in range(-1, 7):
                seq += 1
                row = dict(
                    zip(
                        campaign.FIELDS,
                        [
                            seq,
                            cid,
                            trial,
                            *spec,
                            1,
                            73333334,
                            73333333,
                            100,
                            1,
                            0,
                            1,
                        ],
                    )
                )
                if mutate:
                    mutate(row)
                writer.writerow(row[k] for k in campaign.FIELDS)
        out.write("# complete\n")
        self.path.with_suffix(".extended.csv").write_text(out.getvalue())
        self.path.with_suffix(".validation.json").write_text(
            json.dumps(self.validation)
        )
        return campaign.read_campaign(
            self.path, self.manifest, "hardware-unverified"
        )

    def report(self, mutate=None):
        _, _, rows, validation = self.fixture(mutate)
        host = {r["seq"]: r["seconds"] * 1.25 for r in rows}
        return campaign.summarize(
            rows, rows, host, validation, validation, [1e-6, 1e-6]
        )

    def test_complete_and_host_clock_formula(self):
        _, base, rows, _ = self.fixture()
        self.assertEqual(len(base) + len(rows), 504)
        report = self.report()
        self.assertEqual(len(report), 29)
        self.assertTrue(
            all(abs(r["speed_percent"] - 80) < 1e-6 for r in report)
        )

    def test_warmup_failure_is_not_discarded(self):
        report = self.report(
            lambda r: r.update(ok=0) if r["trial"] == -1 else None
        )
        self.assertTrue(all(r["speed_percent"] is None for r in report))

    def test_memcpy_fallback_is_never_gpu_speed(self):
        report = self.report(
            lambda r: (
                r.update(native=0) if r["case"].startswith("m2mf/") else None
            )
        )
        self.assertTrue(all(r["speed_percent"] is None for r in report[11:]))
        self.assertIsNotNone(report[0]["speed_percent"])

    def test_raw_nested_failure_overrules_pass_summary(self):
        self.validation["tests"][11]["events"] = [
            {
                "scope": "subcase",
                "events": [{"level": "fail", "message": "known CI failure"}],
            }
        ]
        report = self.report()
        self.assertTrue(all(r["speed_percent"] is None for r in report[11:]))

    def test_skipped_cpu_replay_is_not_a_pass(self):
        self.validation["tests"][0]["events"] = [
            {"level": "info", "message": "[SKIP] unsupported"}
        ]
        self.assertIsNone(self.report()[0]["speed_percent"])

    def test_reject_changed_phase_work_count_sequence(self):
        for field in ("phase", "loops", "seq", "tsc"):
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.fixture(lambda r: r.update({field: r[field] + 1}))

    def test_missing_companions_and_incomplete_extension(self):
        self.fixture()
        path = self.path.with_suffix(".extended.csv")
        path.write_text(path.read_text().replace("# complete\n", ""))
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            campaign.read_campaign(
                self.path, self.manifest, "hardware-unverified"
            )
        self.fixture()
        self.path.with_suffix(".validation.json").unlink()
        with self.assertRaises(OSError):
            campaign.read_campaign(
                self.path, self.manifest, "hardware-unverified"
            )

    def test_reject_foreign_validation_and_missing_case(self):
        self.validation["suite"] += "other-session"
        with self.assertRaisesRegex(ValueError, "identity"):
            self.fixture()
        self.validation["suite"] = (
            "xbox-benchmark-pilot-2-validation|" + self.header
        )
        self.validation["tests"].pop()
        with self.assertRaisesRegex(ValueError, "validation cases"):
            self.fixture()

    def test_extension_noise_and_clock_disagreement(self):
        report = self.report(lambda r: r.update(ticks_ms=10000))
        self.assertTrue(all(r["speed_percent"] is None for r in report))
        _, _, rows, validation = self.fixture()
        host = {
            r["seq"]: r["seconds"] * (2 if r["trial"] == 0 else 1)
            for r in rows
        }
        report = campaign.summarize(
            rows, rows, host, validation, validation, [1e-6, 1e-6]
        )
        self.assertTrue(all(r["speed_percent"] is None for r in report))


if __name__ == "__main__":
    unittest.main()
