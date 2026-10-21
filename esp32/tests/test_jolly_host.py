# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host-compiled C unit tests for the ES3C40P landscape and Jolly code (tests/jolly/*.c)."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MUSE = ROOT / 'components/muse'
TESTS = ROOT / 'tests/jolly'

# Each test and the production sources it links.
CASES = {
    'orient': ['boards/es3c40p_orient.c'],
}


def build_and_run(name, sources):
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / name
        cc = shlex.split(os.environ.get('CC', 'cc'))
        cmd = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(MUSE), '-I', str(TESTS),
               str(TESTS / f'test_{name}.c'), *[str(MUSE / s) for s in sources], '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode:
            return build.returncode, build.stdout + build.stderr
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        return run.returncode, run.stdout + run.stderr


class JollyHost(unittest.TestCase):
    def test_orient(self):
        code, out = build_and_run('orient', CASES['orient'])
        self.assertEqual(code, 0, out)


if __name__ == '__main__':
    unittest.main()
