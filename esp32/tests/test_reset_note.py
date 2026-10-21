#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""main/reset_note_text.c: why setup was last reset, as >whyreset prints it."""

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ResetNoteTest(unittest.TestCase):
    def test_reset_note_text(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not cc or shutil.which(cc[0]) is None:
            raise unittest.SkipTest('C compiler not available')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'reset_note'
            build = subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'main'),
                                    str(ROOT / 'tests' / 'reset_note_harness.c'),
                                    str(ROOT / 'main' / 'reset_note_text.c'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
