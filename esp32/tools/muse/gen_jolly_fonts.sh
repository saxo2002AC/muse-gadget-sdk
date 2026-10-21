#!/bin/sh
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Regenerates components/muse/fonts/jolly_font_{12,16,20,28,48d}.c, the Jolly
# screens' fonts (CONFIG_MUSE_JOLLY): Montserrat Medium, as LVGL's built-in
# fonts, with Latin-1 for Spanish ("Guardián", "días", "°") plus the arrows
# and the ellipsis; the 48 px one only digits, "%", "°", ".", "-" and space.
# The font comes from LVGL v9.5.0 (the version the UI uses) and is checked by
# hash. lv_font_conv is taken from PATH if it's 1.5.3 (npm i -g
# lv_font_conv@1.5.3; npx fails on some Macs), else from npx.
set -eu
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$HERE/components/muse/fonts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SHA256=421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53
curl -fsSL -o "$TMP/montserrat.ttf" \
    "https://raw.githubusercontent.com/lvgl/lvgl/v9.5.0/scripts/built_in_font/Montserrat-Medium.ttf"
echo "$SHA256  $TMP/montserrat.ttf" | shasum -a 256 -c - >/dev/null
if command -v lv_font_conv >/dev/null 2>&1 && [ "$(lv_font_conv --version)" = "1.5.3" ]; then
    CONV=lv_font_conv
else
    CONV="npx -y lv_font_conv@1.5.3"
fi
gen() {   # name size ranges...
    name=$1 size=$2
    shift 2
    $CONV --font "$TMP/montserrat.ttf" --size "$size" --bpp 4 --format lvgl \
        --lv-font-name "$name" --no-compress "$@" -o "$TMP/$name.c"
    sed -e 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' -e "s|$TMP/||g" "$TMP/$name.c" > "$OUT/$name.c"
    echo "wrote $OUT/$name.c"
}
LATIN="0x20-0x7E,0xA0-0xFF,0x2191,0x2193,0x2026"
gen jolly_font_12 12 -r "$LATIN"
gen jolly_font_16 16 -r "$LATIN"
gen jolly_font_20 20 -r "$LATIN"
gen jolly_font_28 28 -r "$LATIN"
gen jolly_font_48d 48 -r "0x20,0x25,0x2D,0x2E,0x30-0x39,0xB0"
