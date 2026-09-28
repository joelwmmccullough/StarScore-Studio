#!/usr/bin/env python3
"""
StarScore Studio: adds the chord-symbol triangles to StarScore Jost.

For each chord font family (Jost, Bravura, Petaluma, Leland, MuseJazz, Finale Maestro, Finale Broadway)
two glyphs are made, both normalised to the capital height (700 units) and sitting on the baseline:
  plain triangle (major seventh)          U+E000 + 2*i
  triangle with a bar (minor-major 7th)   U+E001 + 2*i
The triangle is the font's own chord-symbol triangle (SMuFL csymMajorSeventh) where it has one; the bar is
the font's own chord minus (csymMinor), stretched across the triangle, so hand-drawn fonts keep their look.
Jost (i = 0) uses the geometric triangle already in the font.
Diamonds (U+E020 + 2*i diminished-major, U+E021 + 2*i minor-major) are geometric, in each font's line weight. All source fonts are SIL OFL.

Run from the repository root:  python3 fonts/starscorejost/build_chord_triangles.py
"""
import io
import math
import sys

import cairosvg
from PIL import Image
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.reverseContourPen import ReverseContourPen

TARGET = "fonts/starscorejost/StarScoreJost.ttf"
CAP = 700
SIDE = 40

# index, name, source font, triangle code point, minus code point (None = draw a rectangle)
SOURCES = [
    (0, "Jost", TARGET, 0xE000, None),   # Jost's own geometric triangle (already in the font)
    (1, "Bravura", "fonts/bravura/BravuraText.otf", 0xE873, 0xE874),
    (2, "Petaluma", "fonts/petaluma/PetalumaText.otf", 0xE873, 0xE874),
    (3, "Leland", "fonts/leland/LelandText.otf", 0xE873, 0xE874),
    (4, "MuseJazz", "fonts/musejazz/MuseJazzText.otf", 0xE18A, 0x2D),
    (5, "Finale Maestro", "fonts/finalemaestro/FinaleMaestroText-Regular.otf", 0xE873, 0xE874),
    (6, "Finale Broadway", "fonts/finalebroadway/FinaleBroadwayText.otf", 0xE873, None),
]


# Fonts whose triangle strokes are heavy (Jost SemiBold, MuseJazz, Finale Broadway): the bar is drawn lighter
BOLD = {0: 0.5, 4: 0.42, 6: 0.34}   # bar thickness as a share of the triangle's stroke


def record(font, code):
    gs = font.getGlyphSet()
    name = font.getBestCmap().get(code)
    if not name:
        return None, None
    rec = RecordingPen()
    gs[name].draw(rec)
    bp = BoundsPen(gs)
    gs[name].draw(bp)
    return rec, bp.bounds


def raster(rec, transform, width, height):
    """Rasterise a recording at 1 unit = 1 px (y up) and return a PIL image with y flipped to screen space."""
    sp = SVGPathPen(None)
    rec.replay(TransformPen(sp, transform))
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">'
           f'<g transform="translate(0,{height}) scale(1,-1)"><path d="{sp.getCommands()}" fill="black"/></g></svg>')
    rgba = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode()))).convert("RGBA")
    white = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    white.alpha_composite(rgba)
    return white.convert("L")


def measure(img):
    """Base stroke, the counter's middle (y), and the outer edges at that height."""
    w, h = img.size
    px = img.load()

    def dark(x, y):  # y up
        a = px[x, h - 1 - y]
        return a < 128 if isinstance(a, int) else a[0] < 128

    cols = [x for x in range(w) if any(dark(x, y) for y in range(0, h, 4))]
    cx = (min(cols) + max(cols)) // 2
    y = 0
    while y < h and not dark(cx, y):
        y += 1
    base0 = y
    while y < h and dark(cx, y):
        y += 1
    stroke = max(20, y - base0)
    gap0 = y
    while y < h and not dark(cx, y):
        y += 1
    gap1 = y if y < h else int(h * 0.8)
    mid = (gap0 + gap1) // 2
    row = [x for x in range(w) if dark(x, mid)]
    return stroke, mid, (min(row), max(row)) if row else (min(cols), max(cols))


def draw_bar(pen, minus, x0, x1, y0, y1):
    if minus is None:
        # counter-clockwise here, like the CFF outlines, so it ends up clockwise after the TrueType reversal
        pen.moveTo((round(x0), round(y0)))
        pen.lineTo((round(x1), round(y0)))
        pen.lineTo((round(x1), round(y1)))
        pen.lineTo((round(x0), round(y1)))
        pen.closePath()
        return
    rec, (bx0, by0, bx1, by1) = minus
    sx = (x1 - x0) / (bx1 - bx0)
    sy = (y1 - y0) / (by1 - by0)
    rec.replay(TransformPen(pen, (sx, 0, 0, sy, x0 - bx0 * sx, y0 - by0 * sy)))


def glyph_from(draw):
    tt = TTGlyphPen(None)
    draw(Cu2QuPen(tt, 1.0, reverse_direction=True))
    return tt.glyph()


def build_variant(target, index, src_path, tri_code, minus_code):
    src = target if src_path == TARGET else TTFont(src_path)
    rec, (x0, y0, x1, y1) = record(src, tri_code)
    s = CAP / (y1 - y0)
    tx = SIDE - x0 * s
    ty = -y0 * s
    t = (s, 0, 0, s, tx, ty)
    width = int((x1 - x0) * s + 2 * SIDE)
    img = raster(rec, t, width + 1, CAP + 1)
    stroke, mid, (xl, xr) = measure(img)
    minus = None
    if minus_code:
        mrec, mb = record(src, minus_code)
        if mrec:
            minus = (mrec, mb)
    if index in BOLD:
        # Heavy triangles: a lighter bar that ends closer to the sides keeps the symbol open
        thick = max(30, stroke * BOLD[index])
        ext = max(40, stroke * 0.38)
    else:
        thick = min(110, max(40, stroke * 0.85))
        ext = min(110, max(70, stroke * 0.6))
    bx0, bx1 = xl - ext, xr + ext
    shift = max(0, SIDE - bx0)

    def plain(pen):
        # A TrueType source (Jost's own triangle) already runs clockwise: undo the reversal applied when writing
        target_pen = ReverseContourPen(pen) if src is target else pen
        rec.replay(TransformPen(target_pen, (s, 0, 0, s, tx + shift, ty)))

    def barred(pen):
        plain(pen)
        draw_bar(pen, minus, bx0 + shift, bx1 + shift, mid - thick / 2, mid + thick / 2)

    adv_plain = width
    adv_bar = int(max(width + shift, bx1 + shift + SIDE))
    add_glyph(target, 0xE000 + 2 * index, plain if shift == 0 else lambda p: rec.replay(TransformPen(ReverseContourPen(p) if src is target else p, t)), adv_plain)
    add_glyph(target, 0xE001 + 2 * index, barred, adv_bar)
    build_diamonds(target, index, stroke)


# Diamond line weight relative to the font's triangle stroke (Petaluma's hand-drawn stroke reads heavy on a diamond)
DIAMOND_WEIGHT = {2: 0.66}
# Fonts with heavy lines: the minor-major bar is drawn only outside the diamond (two short ticks), so the
# small opening in the middle stays clear
DIAMOND_TICKS = {0, 4, 6}


def poly(pen, pts):
    pen.moveTo(pts[0])
    for p in pts[1:]:
        pen.lineTo(p)
    pen.closePath()


def build_diamonds(target, index, stroke, ticks=None):
    """U+E020 + 2*index: diamond (diminished-major seventh); U+E021 + 2*index: diamond with a bar (minor-major)."""
    stroke = max(40, min(130, stroke)) * DIAMOND_WEIGHT.get(index, 1.0)
    ticks = (index in DIAMOND_TICKS) if ticks is None else ticks
    h = CAP
    w = round(h * 0.78)
    a, b = w / 2, h / 2
    c = math.hypot(a, b)
    ia, ib = a - stroke * c / b, b - stroke * c / a
    if index in BOLD:
        thick = max(30, stroke * BOLD[index])
        ext = max(60, stroke * 0.55)
    else:
        thick = min(110, max(40, stroke * (0.7 if index in DIAMOND_WEIGHT else 0.85)))
        ext = min(110, max(70, stroke * 0.6))

    def shape(pen, dx):
        cx, cy = SIDE + dx + a, b
        # outer counter-clockwise and inner clockwise here; both are reversed when written as TrueType
        poly(pen, [(round(cx - a), round(cy)), (round(cx), round(cy - b)), (round(cx + a), round(cy)), (round(cx), round(cy + b))])
        poly(pen, [(round(cx - ia), round(cy)), (round(cx), round(cy + ib)), (round(cx + ia), round(cy)), (round(cx), round(cy - ib))])

    def plain(pen):
        shape(pen, 0)

    def barred(pen):
        shape(pen, ext)
        x0, x1 = SIDE, SIDE + ext + w + ext
        y0, y1 = b - thick / 2, b + thick / 2
        if ticks:
            # only the parts outside the diamond: from the ends to just inside the outer corners
            overlap = min(stroke * 0.5, a * 0.3)
            draw_bar(pen, None, x0, SIDE + ext + overlap, y0, y1)
            draw_bar(pen, None, SIDE + ext + w - overlap, x1, y0, y1)
        else:
            draw_bar(pen, None, x0, x1, y0, y1)

    add_glyph(target, 0xE020 + 2 * index, plain, w + 2 * SIDE)
    add_glyph(target, 0xE021 + 2 * index, barred, w + 2 * SIDE + 2 * ext)


def add_glyph(font, code, draw, advance):
    name = f"uni{code:04X}"
    g = glyph_from(draw)
    g.recalcBounds(font["glyf"])
    font["glyf"][name] = g
    order = font.getGlyphOrder()
    if name not in order:
        order.append(name)
        font.setGlyphOrder(order)
    font["hmtx"][name] = (int(advance), getattr(g, "xMin", 0))
    for table in font["cmap"].tables:
        if table.isUnicode():
            table.cmap[code] = name


def main():
    font = TTFont(TARGET)
    for index, _name, path, tri, minus in SOURCES:
        build_variant(font, index, path, tri, minus)
    font["maxp"].numGlyphs = len(font.getGlyphOrder())
    font.save(TARGET)
    print("ok")


if __name__ == "__main__":
    sys.exit(main())
