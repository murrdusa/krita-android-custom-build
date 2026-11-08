"""Standard-library tests for the non-publishing local APK size audit.

Run: python3 -m unittest discover -s tests -p 'test_apk_size_report.py'
"""
import contextlib
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apk_size_report as report


class ApkSizeReportTests(unittest.TestCase):
    def make_apk(self, directory: Path, filename: str, payload: bytes,
                 abi: str = "arm64-v8a") -> Path:
        apk = directory / filename
        with zipfile.ZipFile(apk, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("classes.dex", b"dex" + payload)
            archive.writestr("assets/krita/brushes/sample", payload)
            archive.writestr(f"lib/{abi}/libkrita.so", payload)
        return apk

    def test_categorization_and_digest(self):
        with tempfile.TemporaryDirectory() as tmp:
            apk = self.make_apk(Path(tmp), "baseline.apk", b"abcdef" * 30)
            data = report.inspect(apk)
            self.assertEqual(data["apk_bytes"], apk.stat().st_size)
            self.assertEqual(data["abis"], ["arm64-v8a"])
            self.assertEqual(len(data["sha256"]), 64)
            self.assertEqual(
                data["categories"]["Native libraries"]["count"], 1)
            self.assertEqual(
                data["categories"]["Bundled assets"]["count"], 1)
            self.assertEqual(
                data["categories"]["Android bytecode"]["count"], 1)
            self.assertEqual(len(data["largest_entries"]), 3)

    def test_history_tracks_changed_apk_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            before = report.inspect(
                self.make_apk(output, "before.apk", b"old-payload" * 15))
            after = report.inspect(
                self.make_apk(output, "after.apk", b"new-payload" * 50))
            with contextlib.redirect_stdout(io.StringIO()):
                report.record_history(output, before)
                report.record_history(output, before)
            history = output / "apk-size-history.json"
            self.assertEqual(
                len(json.loads(history.read_text())["snapshots"]), 1)
            with contextlib.redirect_stdout(io.StringIO()):
                report.record_history(output, after)
            saved = json.loads(history.read_text())["snapshots"]
            self.assertEqual(len(saved), 2)
            self.assertEqual(saved[-1]["sha256"], after["sha256"])
            self.assertNotEqual(saved[0]["sha256"], saved[-1]["sha256"])

    def test_report_writes_without_modifying_apk(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            apk = self.make_apk(output, "current.apk", b"paintbrush" * 30)
            original = apk.read_bytes()
            old_args = sys.argv
            try:
                sys.argv = ["apk_size_report.py", str(output)]
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(report.main(), 0)
            finally:
                sys.argv = old_args
            self.assertEqual(apk.read_bytes(), original)
            self.assertTrue((output / "apk-size-report.json").is_file())
            self.assertTrue((output / "apk-size-history.json").is_file())


if __name__ == "__main__":
    unittest.main()
