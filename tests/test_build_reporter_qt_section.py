#!/usr/bin/env python3
"""Dependency-free regression tests for the local build issue reporter."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("krita_build_reporter", ROOT / "krita_build_reporter.py")
reporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reporter)


class QtLifecycleReportTests(unittest.TestCase):
    def test_large_log_preserves_only_latest_twelve_verdicts(self):
        with tempfile.TemporaryDirectory() as work:
            base = Path(work)
            logs = base / "logs"
            logs.mkdir()
            path = logs / "large.log"
            with path.open("w", encoding="utf-8") as log:
                for i in range(20000):
                    log.write(f"compiler output {i}\\n")
                    if i % 1000 == 0:
                        log.write(f"Native Qt lifecycle tests: test {i}\\n")
                log.write("Native Qt lifecycle tests: PASS (3 executables)\\n")
            with patch.object(reporter, "BASE", base):
                section = reporter.qt_lifecycle_section(f"Full local log: `{path}`")
            self.assertIn("Native Qt lifecycle tests: PASS", section)
            self.assertEqual(section.count("Native Qt lifecycle tests: "), 12)
            self.assertNotIn("Native Qt lifecycle tests: test 0\\n", section)

    def test_rejects_log_outside_watcher_directory(self):
        with tempfile.TemporaryDirectory() as work:
            base = Path(work)
            (base / "logs").mkdir()
            outside = base / "untrusted.log"
            outside.write_text("Native Qt lifecycle tests: PASS\\n", encoding="utf-8")
            with patch.object(reporter, "BASE", base):
                section = reporter.qt_lifecycle_section(f"Full local log: `{outside}`")
            self.assertIn("outside watcher directory", section)
            self.assertNotIn("tests: PASS", section)

    def test_missing_verdict_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as work:
            base = Path(work)
            logs = base / "logs"
            logs.mkdir()
            path = logs / "no-test.log"
            path.write_text("Android APK build: SUCCESS\\n", encoding="utf-8")
            with patch.object(reporter, "BASE", base):
                section = reporter.qt_lifecycle_section(f"Full local log: `{path}`")
            self.assertIn("NOT REPORTED", section)


if __name__ == "__main__":
    unittest.main()
