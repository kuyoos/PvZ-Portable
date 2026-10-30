#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generate a PopCap/Sexy-format bitmap font atlas for Chinese UI text.

The engine reads ``data/<name>.txt`` (descriptor) and ``data/<name>.png``
(atlas) in the same directory; the descriptor format is the one used by
BrianneTod16, e.g.::

    Define CharList
     ('A', 'B', ...);
    Define WidthList
     ( 11, 13, ...);
    Define RectList
     ( (0, 0, 12, 23), ...);
    CreateLayer               Main;
    LayerSetImage             Main 'pvz_cn';
    LayerSetAscent            Main 19;
    LayerSetCharWidths        Main CharList WidthList;
    LayerSetImageMap          Main CharList RectList;
    LayerSetAscentPadding     Main 0;
    LayerSetLineSpacingOffset Main 0;
    LayerSetPointSize         Main 16;

Usage::

    python tools/font/gen_cjk_font.py --font NotoSansSC-Regular.otf --out assets/font

The font file itself is never committed; only the generated atlas and
descriptor are (glyphs of Noto Sans SC are OFL-1.1).
"""

import argparse
import pathlib
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - developer tool
    sys.exit("需要 Pillow：python -m pip install pillow")


def gb2312_chars(rows):
    """Decode GB2312 rows (1-based) into a list of characters."""
    out = []
    seen = set()
    for row in rows:
        for cell in range(1, 95):
            try:
                ch = bytes([0xA0 + row, 0xA0 + cell]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if ch in seen:
                continue
            seen.add(ch)
            out.append(ch)
    return out


def build_charlist(include_ascii=True, level2=False, extra=""):
    chars = []
    seen = set()

    def add(ch):
        # The descriptor is single-quote delimited and the engine tokenizer has
        # no escape sequence for these two, so they cannot be represented.
        if ch in ("'", "\\", "\r", "\n"):
            return
        if ch in seen:
            return
        seen.add(ch)
        chars.append(ch)

    if include_ascii:
        for code in range(0x20, 0x7F):
            add(chr(code))
    # GB2312 rows 1..9: punctuation, kana, Greek, Cyrillic, pinyin.
    for ch in gb2312_chars(range(1, 10)):
        add(ch)
    # Rows 16..55: level 1 hanzi (3755, pinyin order).
    for ch in gb2312_chars(range(16, 56)):
        add(ch)
    if level2:
        for ch in gb2312_chars(range(56, 88)):
            add(ch)
    for ch in extra:
        add(ch)
    return chars


def render(font_path, size, chars, max_width=1024):
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()

    glyphs = []
    for ch in chars:
        try:
            advance = int(round(font.getlength(ch)))
            x0, y0, x1, y1 = font.getbbox(ch)
        except Exception:
            glyphs.append((ch, size // 2, 0, 0, 0, 0, 0))
            continue
        w = max(0, x1 - x0)
        h = max(0, y1 - y0)
        glyphs.append((ch, advance, w, h, x0, y0, y1 - y0))

    pad = 1
    x = pad
    y = pad
    row_h = 0
    placements = []
    for ch, advance, w, h, x0, y0, _ in glyphs:
        cw = max(1, w)
        chh = max(1, h)
        if x + cw + pad > max_width:
            x = pad
            y += row_h + pad
            row_h = 0
        placements.append((x, y, cw, chh))
        x += cw + pad
        row_h = max(row_h, chh)
    atlas_w = max_width
    atlas_h = max(y + row_h + pad, size * 2)

    atlas = Image.new("RGBA", (atlas_w, atlas_h), (255, 255, 255, 0))
    draw = ImageDraw.Draw(atlas)
    for (ch, advance, w, h, x0, y0, _), (px, py, cw, chh) in zip(glyphs, placements):
        if w <= 0 or h <= 0:
            continue
        draw.text((px - x0, py - y0), ch, font=font, fill=(255, 255, 255, 255))

    return font, ascent, descent, atlas, glyphs, placements


def dump_descriptor(path, name, size, chars, glyphs, placements, ascent):
    lines = ["Define CharList"]
    for start in range(0, len(chars), 16):
        chunk = chars[start:start + 16]
        text = "  ".join("'%s'," % c for c in chunk)
        lines.append(" " + text)
    lines[-1] = lines[-1].rstrip(",") + " );"

    lines.append("Define WidthList")
    widths = [str(g[1]) for g in glyphs]
    for start in range(0, len(widths), 20):
        chunk = widths[start:start + 20]
        lines.append(" " + ", ".join(chunk) + ",")
    lines[-1] = lines[-1].rstrip(",") + " );"

    lines.append("Define RectList")
    rects = ["(%d, %d, %d, %d)" % p for p in placements]
    for start in range(0, len(rects), 8):
        chunk = rects[start:start + 8]
        lines.append(" " + ", ".join(chunk) + ",")
    lines[-1] = lines[-1].rstrip(",") + " );"

    lines += [
        "",
        "CreateLayer               Main;",
        "LayerSetImage             Main '%s';" % name,
        "LayerSetAscent            Main %d;" % ascent,
        "LayerSetCharWidths        Main CharList WidthList;",
        "LayerSetImageMap          Main CharList RectList;",
        "LayerSetAscentPadding     Main 0;",
        "LayerSetLineSpacingOffset Main 0;",
        "LayerSetPointSize         Main %d;" % size,
        "",
        "SetDefaultPointSize  %d;" % size,
        "",
    ]
    # The engine decodes char entries as UTF-8 (UTF8CharToUTF32Char).
    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--name", default="pvz_cn")
    parser.add_argument("--size", type=int, default=16)
    parser.add_argument("--level2", action="store_true", help="also include GB2312 level 2 hanzi")
    parser.add_argument("--extra", default="，。！？：；、（）「」《》…—·")
    args = parser.parse_args()

    chars = build_charlist(level2=args.level2, extra=args.extra)
    font, ascent, descent, atlas, glyphs, placements = render(args.font, args.size, chars)

    args.out.mkdir(parents=True, exist_ok=True)
    png_path = args.out / (args.name + ".png")
    txt_path = args.out / (args.name + ".txt")
    atlas.save(png_path, optimize=True)
    dump_descriptor(txt_path, args.name, args.size, chars, glyphs, placements, ascent)

    print("chars: %d (CJK %d)" % (len(chars), sum(1 for c in chars if ord(c) > 0x2E80)))
    print("atlas: %dx%d -> %s (%.1f KB)" % (atlas.width, atlas.height, png_path, png_path.stat().st_size / 1024))
    print("descriptor: %s (%.1f KB)" % (txt_path, txt_path.stat().st_size / 1024))


if __name__ == "__main__":
    main()
