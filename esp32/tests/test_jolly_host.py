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
    'glance': ['jolly/jolly_glance.c'],
    'text': ['jolly/jolly_text.c'],
    'gesture': ['jolly/jolly_gesture.c'],
    'motion': ['jolly/jolly_motion.c', 'jolly/jolly_mood.c'],
    'face': ['jolly/jolly_face_spec.c'],
    'alarm': ['jolly/jolly_alarm.c'],
}
JSON = Path(os.environ.get('CJSON_SOURCE_DIR', ROOT / 'managed_components/espressif__cjson/cJSON'))


def build_and_run(name, sources, extra=(), args=()):
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / name
        cc = shlex.split(os.environ.get('CC', 'cc'))
        cmd = [*cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(MUSE), '-I', str(TESTS),
               str(TESTS / f'test_{name}.c'), *[str(MUSE / s) for s in sources], *extra, '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode:
            return build.returncode, build.stdout + build.stderr
        run = subprocess.run([str(exe), *args], capture_output=True, text=True)
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
    def test_no_label_recolor(self):
        """R16: server text never goes through LVGL's #rrggbb recolour."""
        offenders = [str(p) for p in (MUSE / 'jolly').glob('*.c')
                     if re.search(r'lv_label_set_recolor|LV_TEXT_FLAG_RECOLOR', p.read_text())]
        self.assertEqual(offenders, [])

    def test_fonts_cover_spanish(self):
        for size in (12, 16, 20, 28):
            points = font_codepoints(FONTS / f'jolly_font_{size}.c')
            missing = [hex(c) for c in SPANISH if c not in points]
            self.assertEqual(missing, [], f'jolly_font_{size}')
        points = font_codepoints(FONTS / 'jolly_font_48d.c')
        self.assertEqual([hex(c) for c in DIGITS if c not in points], [], 'jolly_font_48d')
        self.assertNotIn(0x2190, font_codepoints(FONTS / 'jolly_font_16.c'))   # the parser can say no

    def test_orient(self):
        code, out = build_and_run('orient', CASES['orient'])
        self.assertEqual(code, 0, out)

    def test_glance(self):
        # cJSON comes from managed_components/, which only exists after a build or
        # reconfigure (as for test_link_discovery), or CJSON_SOURCE_DIR.
        if not (JSON / 'cJSON.c').exists():
            self.skipTest(f'no cJSON at {JSON}: run idf.py reconfigure first')
        code, out = build_and_run('glance', CASES['glance'], extra=['-I', str(JSON), str(JSON / 'cJSON.c')],
                                  args=[str(TESTS)])
        self.assertEqual(code, 0, out)

    def test_gesture(self):
        code, out = build_and_run('gesture', CASES['gesture'])
        self.assertEqual(code, 0, out)

    def test_motion(self):
        code, out = build_and_run('motion', CASES['motion'], extra=['-lm'])
        self.assertEqual(code, 0, out)

    def test_alarm(self):
        code, out = build_and_run('alarm', CASES['alarm'])
        self.assertEqual(code, 0, out)

    def test_face(self):
        code, out = build_and_run('face', CASES['face'], extra=['-lm'])
        self.assertEqual(code, 0, out)

    def test_mood(self):
        code, out = build_and_run('mood', CASES['mood'])
        self.assertEqual(code, 0, out)

    def test_text(self):
        code, out = build_and_run('text', CASES['text'])
        self.assertEqual(code, 0, out)

    def test_url(self):
        code, out = build_and_run('url', CASES['url'])
        self.assertEqual(code, 0, out)


if __name__ == '__main__':
    unittest.main()
