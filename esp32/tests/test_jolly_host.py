# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Host-compiled C unit tests for the ES3C40P landscape and Jolly code (tests/jolly/*.c)."""
import os
import re
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
    'url': ['muse_url.c'],
    'mood': ['jolly/jolly_mood.c'],
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


FONTS = MUSE / 'fonts'
SPANISH = [0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xF1, 0xFC, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xD1, 0xBF, 0xA1,
           0xB0, 0x2191, 0x2193, 0x2026] + list(range(0x20, 0x7F))
DIGITS = [ord(c) for c in '0123456789%.- '] + [0xB0]


def font_codepoints(path):
    """The code points an lv_font_conv font maps: its cmaps, dense or sparse."""
    src = Path(path).read_text()
    lists = {name: [int(v, 0) for v in body.replace('\n', ' ').split(',') if v.strip()]
             for name, body in re.findall(r'static const uint16_t (unicode_list_\d+)\[\] = \{([^}]*)\}', src)}
    points = set()
    for start, length, ulist in re.findall(
            r'\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = \d+,\s*'
            r'\.unicode_list = (\w+)', src):
        start, length = int(start), int(length)
        if ulist == 'NULL':
            points.update(range(start, start + length))
        else:
            points.update(start + off for off in lists[ulist])
    return points


class JollyHost(unittest.TestCase):
    def test_fonts_cover_spanish(self):
        for size in (16, 20, 28):
            points = font_codepoints(FONTS / f'jolly_font_{size}.c')
            missing = [hex(c) for c in SPANISH if c not in points]
            self.assertEqual(missing, [], f'jolly_font_{size}')
        points = font_codepoints(FONTS / 'jolly_font_48d.c')
        self.assertEqual([hex(c) for c in DIGITS if c not in points], [], 'jolly_font_48d')
        self.assertNotIn(0x2190, font_codepoints(FONTS / 'jolly_font_16.c'))   # the parser can say no

    def test_orient(self):
        code, out = build_and_run('orient', CASES['orient'])
        self.assertEqual(code, 0, out)

    def test_mood(self):
        code, out = build_and_run('mood', CASES['mood'])
        self.assertEqual(code, 0, out)

    def test_url(self):
        code, out = build_and_run('url', CASES['url'])
        self.assertEqual(code, 0, out)


if __name__ == '__main__':
    unittest.main()
