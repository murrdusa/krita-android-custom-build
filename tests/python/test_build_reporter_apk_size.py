"""Test GitHub dashboard APK size formatting without making network calls."""
import json
import tempfile
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "build"))
import krita_build_reporter as reporter


class BuildReporterSizeTests(unittest.TestCase):
    def test_missing_report_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIn("No local size report",
                          reporter.apk_size_section(Path(tmp)))

    def test_report_summarizes_categories_and_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "persistent" / "krita-custom-apks"
            output.mkdir(parents=True)
            apk = {
                "apk": "krita-preview.apk",
                "apk_bytes": 8*1024*1024,
                "sha256": "new",
                "categories": {
                    "Native libraries": {"compressed_bytes": 6*1024*1024},
                    "Bundled assets": {"compressed_bytes": 1024*1024},
                },
                "largest_entries": [
                    {"path": "lib/arm64-v8a/libkrita.so",
                     "compressed_bytes": 4*1024*1024}
                ],
            }
            (output / "apk-size-report.json").write_text(
                json.dumps({"apks": [apk]}), encoding="utf-8")
            (output / "apk-size-history.json").write_text(
                json.dumps({"snapshots": [
                    {"sha256": "old", "apk_bytes": 10*1024*1024},
                    {"sha256": "new", "apk_bytes": 8*1024*1024},
                ]}), encoding="utf-8")
            rendered = reporter.apk_size_section(Path(tmp))
            self.assertIn("8.00 MiB", rendered)
            self.assertIn("Native libraries", rendered)
            self.assertIn("75.0%", rendered)
            self.assertIn("-2.00 MiB", rendered)
            self.assertIn("libkrita.so", rendered)
            self.assertIn("not uploaded", rendered)
            self.assertNotIn("https://", rendered)


if __name__ == "__main__":
    unittest.main()
