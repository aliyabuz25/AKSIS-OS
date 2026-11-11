Selawik Regular is Microsoft's OFL-licensed open source alternative to Segoe UI.

Font source: https://github.com/microsoft/MRDL_Unity_PeriodicTable/blob/master/Assets/MRDL_PeriodicTable/Fonts/selawk.ttf

Upstream: https://github.com/microsoft/Selawik

License: [OFL.txt](OFL.txt). The generated grayscale bitmap subset is named
Shenlong UI, derived from Selawik. Regenerate it with `python3 tools/build-ui-font.py`
(Pillow required). It covers ASCII and Turkish letters at 18, 26 and 44 px,
rasterized at 4x and reduced with Lanczos coverage filtering.
The same generator creates Turkish terminal glyphs in `kernel/terminal_extra.h`.
The tiny Turkish lira glyph is original cell artwork because this Selawik file
does not include it. The generated font remains named Shenlong UI.

The active terminal now uses a grayscale 10x24 cell atlas, generated from
Noto Sans Mono Regular at 16 px (4x rasterization). It replaces the legacy
one-bit 8x16 font; the old headers are retained for historical tools only.
Source: https://github.com/google/fonts/tree/main/ofl/notosansmono
License: [NotoSansMono-OFL.txt](NotoSansMono-OFL.txt). The font and license
are bundled, and the license is also included in the ISO.
