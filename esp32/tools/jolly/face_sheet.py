#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Face v5's 16 moods from the simulator, as one contact sheet.

    tools/jolly/face_sheet.py SIMULATOR OUT.png

SIMULATOR is a muse_simulator built with -DMUSE_SIM_JOLLY=ON. Each mood is
held with the simulator's mood= key (jolly_eyes_show), snapped with no
motion, on the ES3C40P's 480x320 profile. Needs Pillow.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

MOODS = ('tranquilo escuchando pensando hablando feliz carino durmiendo espia alerta desconectado '
         'mareado asustado aliviado enojado sorprendido guino').split()
W, H, LABEL, GAP, COLS = 480, 320, 30, 12, 4


def shoot(sim, mood, tmp):
    scenario = Path(tmp) / f'{mood}.txt'
    shot = Path(tmp) / f'{mood}.ppm'
    level = 'level=0.45\n' if mood == 'hablando' else ''
    scenario.write_text('wifi=connected\npaired=true\nface=idle\njolly=ok\nadvance=500\n'
                        f'{level}mood={mood}\nadvance=600\n')
    env = dict(os.environ, MUSE_SIM_BOARD='es3c40p')
    subprocess.run([sim, '--headless', '--scenario', str(scenario), '--screenshot', str(shot)],
                   env=env, check=True, capture_output=True)
    return Image.open(shot).convert('RGB')


def font(size):
    for path in ('/System/Library/Fonts/Helvetica.ttc', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sim, out = sys.argv[1], sys.argv[2]
    rows = (len(MOODS) + COLS - 1) // COLS
    sheet = Image.new('RGB', (COLS * (W + GAP) + GAP, rows * (H + LABEL + GAP) + GAP), (24, 26, 40))
    draw = ImageDraw.Draw(sheet)
    f = font(20)
    with tempfile.TemporaryDirectory() as tmp:
        for i, mood in enumerate(MOODS):
            x = GAP + (i % COLS) * (W + GAP)
            y = GAP + (i // COLS) * (H + LABEL + GAP)
            draw.text((x, y + 2), mood, fill=(230, 230, 240), font=f)
            sheet.paste(shoot(sim, mood, tmp), (x, y + LABEL))
    sheet.save(out, optimize=True)
    print(out)


if __name__ == '__main__':
    main()
