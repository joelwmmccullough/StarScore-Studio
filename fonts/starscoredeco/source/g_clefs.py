from kit import *
from shapes import *
from registry import add

BOWL = Nib(0.10, 0.40, 1.4)
STEM = Nib(0.10, 0.22, 1.4)


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
    body = stroke(skel, BOWL, cap0='perp')
    cb = caps()
    dot_c = (0.58, -2.02)
    stem = stroke([apex, ('C', (1.74, 4.22), (1.55, 3.98), (1.55, 3.62)),
                   ('L', (1.55, -1.60)),
                   ('C', (1.55, -2.25), (1.25, -2.56), (0.95, -2.56)),
                   ('C', (0.74, -2.56), (0.60, -2.40), dot_c)], STEM, cap1='perp')
    cs = caps()
    apx = join_point((cb[2], cb[3]), (cs[0], cs[1]), (apex[0] + 0.02, apex[1] + 0.07))
    dot = term(dot_c[0], dot_c[1], 0.52)
    return union(body, stem, apx, dot)


g = gclef()
x0, y0, x1, y1 = bounds(g)
g = move(g, -x0, 0)
add("gClef", g)


def fclef():
    skel = [(0.40, -0.06),
            ('C', (0.40, 0.62), (0.80, 1.00), (1.20, 1.00)),
            ('C', (1.70, 1.00), (2.02, 0.62), (2.02, 0.05)),
            ('C', (2.02, -0.45), (1.77, -1.06), (1.40, -1.40)),
            ('L', (0.20, -2.50))]
    body = stroke(skel, Nib(0.10, 0.42, 1.4), cap1='perp')
    knob = term(0.40, -0.06, 0.58)
    d1 = term(2.52, 0.5, 0.36)
    d2 = term(2.52, -0.5, 0.36)
    return union(body, knob, d1, d2)


fc = fclef()
x0, y0, x1, y1 = bounds(fc)
fc = move(fc, -x0, 0)
add("fClef", fc)


def cclef():
    bars = union(rect(0, -2.0, 0.52, 2.0), rect(0.72, -2.0, 0.86, 2.0))
    nib = Nib(0.10, 0.40, 1.4)
    yb = 0.46                                   # height of the bowl's thin bottom stroke
    skel = [(1.32, 1.44),
            ('C', (1.32, 1.82), (1.62, 1.95), (1.98, 1.95)),
            ('C', (2.40, 1.95), (2.64, 1.62), (2.64, 1.24)),
            ('C', (2.64, 0.80), (2.36, yb), (1.92, yb)),
            ('L', (1.26, yb))]
    bowl_ = stroke(skel, nib, cap1='v')
    half = nib.wmin / 2
    arm = poly([(0.86, 0.0), (1.26, yb - half), (1.26, yb + half), (0.86, 0.17)])
    upper = union(bowl_, term(1.32, 1.44, 0.46), arm)
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
