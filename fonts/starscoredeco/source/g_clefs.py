from kit import *
from shapes import *
from registry import add

import style
import os
CONTRAST = os.environ.get('DECO_CLEF', 'c') == 'c'
BOWL = Nib(0.10, 0.42, 1.5) if CONTRAST else style.pen(0.34)
STEM = style.pen(0.20) if CONTRAST else style.pen(0.27)


def gclef():
    s0, a1 = arc(1.55, 0.05, 0.70, 0.55, 235, 180)
    _, a2 = arc(1.55, 0.05, 0.70, 0.90, 180, 90)
    _, a3 = arc(1.55, 0.00, 1.05, 0.95, 90, 0)
    _, a4 = arc(1.45, 0.00, 1.15, 0.95, 0, -90)
    _, a5 = arc(1.45, 0.10, 1.30, 1.05, -90, -210)
    ex, ey = 1.45 + 1.30 * math.cos(math.radians(150)), 0.10 + 1.05 * math.sin(math.radians(150))
    tx, ty = 1.30 * math.sin(math.radians(150)), -1.05 * math.cos(math.radians(150))
    L = math.hypot(tx, ty); tx, ty = tx / L, ty / L
    dl = (2.68 - ey) / ty
    dx_, dy_ = ex + tx * dl, 2.68
    apex = (1.94, 4.50)
    skel = [s0] + a1 + a2 + a3 + a4 + a5 + [
        ('L', (dx_, dy_)),
        ('C', (dx_ + tx * 0.34, dy_ + ty * 0.34), (2.31, 3.10), (2.31, 3.50)),
        ('C', (2.31, 3.92), (2.13, 4.22), apex),
    ]
    body = stroke(skel, BOWL, cap0='perp', ball0=(0.40, 0.45) if CONTRAST else None)
    cb = caps()
    dot_c = (0.58, -2.02)
    stem = stroke([apex, ('C', (1.74, 4.22), (1.55, 3.98), (1.55, 3.62)),
                   ('L', (1.55, -1.60)),
                   ('C', (1.55, -2.25), (1.25, -2.56), (0.95, -2.56)),
                   ('C', (0.74, -2.56), (0.60, -2.40), dot_c)], STEM, ball1=(0.60, 0.95))
    cs = caps()
    from kit import _lineint
    hb = (apex[0] - 2.13, apex[1] - 4.22)          # body's heading into the apex
    hs = (1.74 - apex[0], 4.22 - apex[1])          # stem's heading out of it
    tip = _lineint(cb[3], hb, cs[1], (-hs[0], -hs[1])) or (apex[0] + 0.02, apex[1] + 0.07)
    apx = join_point((cb[2], cb[3]), (cs[0], cs[1]), tip)
    return union(body, stem, apx)


g = gclef()
x0, y0, x1, y1 = bounds(g)
g = move(g, -x0, 0)
add("gClef", g)


def fclef():
    """Ball head on the F line, a hairline over the top, a heavy right side, and a tail that
    sweeps down to the left and thins to a point."""
    skel = [(0.42, 0.0),
            ('C', (0.42, 0.64), (0.82, 1.04), (1.28, 1.04)),
            ('C', (1.82, 1.04), (2.16, 0.64), (2.16, 0.02)),
            ('C', (2.16, -0.86), (1.44, -1.74), (0.18, -2.46))]
    if CONTRAST:
        taper = lambda u: 1.0 if u < 0.52 else max(0.22, 1.0 - (u - 0.52) / 0.48 * 0.80)
        body = stroke(skel, Nib(0.11, 0.46, 1.5), cap1='perp', ball0=(0.72, 0.62), wfun=taper)
    else:
        body = stroke(skel, style.pen(0.36), cap1='perp', ball0=(0.66, 0.85))
    d1 = term(2.66, 0.5, 0.36)
    d2 = term(2.66, -0.5, 0.36)
    return union(body, d1, d2)


fc = fclef()
x0, y0, x1, y1 = bounds(fc)
fc = move(fc, -x0, 0)
add("fClef", fc)


def cclef():
    bars = union(rect(0, -2.0, 0.52, 2.0), rect(0.72, -2.0, 0.94, 2.0))
    nib = Nib(0.12, 0.40, 1.5) if CONTRAST else style.pen(0.34)
    yb = 0.46                                   # height of the bowl's thin bottom stroke
    skel = [(1.32, 1.44),
            ('C', (1.32, 1.82), (1.62, 1.95), (1.98, 1.95)),
            ('C', (2.40, 1.95), (2.64, 1.62), (2.64, 1.24)),
            ('C', (2.64, 0.80), (2.36, yb), (1.92, yb)),
            ('L', (1.26, yb))]
    bowl_ = stroke(skel, nib, cap1='v', ball0=(0.54, 0.95))
    half = nib.wmin / 2
    arm = poly([(0.86, 0.0), (1.26, yb - half), (1.26, yb + half), (0.86, 0.17)])
    upper = union(bowl_, arm)
    lower = mirror_y(upper)
    return union(bars, upper, lower)


add("cClef", cclef())


def perc():
    return union(rect(0.0, -1.0, 0.36, 1.0), rect(0.9, -1.0, 1.26, 1.0))


add("unpitchedPercussionClef1", perc())

# change (courtesy) clefs at 0.8 size
for n, src in (("gClefChange", "gClef"), ("fClefChange", "fClef"), ("cClefChange", "cClef")):
    import registry
    add(n, scale(registry.G[src]["path"], 0.8))
