# Fonts

`muse_font_cjk_16.c` is the CJK fallback for the caption font, built in only
with `CONFIG_MUSE_CJK_FONT`. It holds GNU Unifont 16.0.04's 16x16 bitmaps,
the same cell as unscii-16, for CJK punctuation, kana, every CJK Unified
Ideograph (U+4E00 to U+9FFF) and the fullwidth forms: about 850 KB of flash.
`tools/muse/gen_cjk_font.sh` regenerates it.

GNU Unifont is by Roman Czyborra, Paul Hardy and contributors
(https://unifoundry.com/unifont/). Its compiled fonts are licensed under the
SIL Open Font License, version 1.1 (https://openfontlicense.org), and under
the GNU GPL version 2 or later with the GNU font embedding exception. This
file is a conversion of an unaltered subset of the font.

`jolly_font_12.c`, `jolly_font_16.c`, `jolly_font_20.c`, `jolly_font_28.c` and `jolly_font_48d.c`
are the Jolly screens' fonts, built in only with `CONFIG_MUSE_JOLLY`: Montserrat
Medium (as LVGL's built-in fonts) at 4 bpp, with ASCII, Latin-1 (U+00A0 to
U+00FF, for Spanish), the arrows U+2191 and U+2193 and the ellipsis U+2026. The
48 px one has only the digits, "%", "°", ".", "-" and space.
`tools/muse/gen_jolly_fonts.sh` regenerates them from LVGL v9.5.0's
`scripts/built_in_font/Montserrat-Medium.ttf` (checked by hash) with
lv_font_conv 1.5.3, and `tests/test_jolly_host.py` checks their coverage.

Montserrat is copyright 2011 The Montserrat Project Authors
(https://github.com/JulietaUla/Montserrat), licensed under the SIL Open Font
License, version 1.1 (https://openfontlicense.org). These files are
conversions of an unaltered subset of the font.
