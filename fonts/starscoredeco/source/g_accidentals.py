import math
from kit import *
from shapes import *
from registry import add

RISE = 0.24   # sharp/natural bar slope over the glyph width
VT = 0.22     # vertical
BT = 0.30     # bar (vertical thickness)


def _vertical(x, y0, y1, w, k):
    """Vertical stroke whose ends are cut parallel to the bars (slope k)."""
    return poly([(x, y0 + k * x), (x + w, y0 + k * (x + w)), (x + w, y1 + k * (x + w)), (x, y1 + k * x)])


def sharp():
    W = 0.96
    k = RISE / W
    xm = W / 2
    v1 = _vertical(0.20, -1.40 - k * xm + 0.06, 1.28 - k * xm + 0.06, VT, k)
    v2 = _vertical(W - 0.20 - VT, -1.28 - k * xm, 1.40 - k * xm, VT, k)
    b1 = parallelogram(0, W, 0.50, BT, RISE)
    b2 = parallelogram(0, W, -0.50, BT, RISE)
    return union(v1, v2, b1, b2)


def natural():
    """Two verticals and two slanted bars. Every end lies exactly on an edge it meets: the
    upper right vertical stops flush with the upper bar's top edge and the lower left vertical
    flush with the lower bar's bottom edge, so there are no small steps."""
    W = 0.66
    k = RISE / W
    xm = W / 2
    yb1, yb2 = 0.46, -0.46                       # bar centres at mid-width
    top_edge = lambda x: yb1 + BT / 2 + k * (x - xm)     # upper bar, top edge
    bot_edge = lambda x: yb2 - BT / 2 + k * (x - xm)     # lower bar, bottom edge
    v1 = poly([(0.0, bot_edge(0.0)), (VT, bot_edge(VT)), (VT, 1.36 + k * (VT - xm)), (0.0, 1.36 + k * (0 - xm))])
    v2 = poly([(W - VT, -1.36 + k * (W - VT - xm)), (W, -1.36 + k * (W - xm)), (W, top_edge(W)), (W - VT, top_edge(W - VT))])
    b1 = parallelogram(0, W, yb1, BT, RISE)
    b2 = parallelogram(0, W, yb2, BT, RISE)
    return union(v1, v2, b1, b2)


def flat(stem_top=1.75):
    S = 0.22                                    # stem width
    stem = rect(0.0, -0.64, S, stem_top)
    # the bowl's lower stroke comes into the stem horizontally, so the bottom is full and
    # round instead of ending in a sharp point
    outer = shape([(S, 0.50),
                   ('C', (0.44, 0.68), (0.84, 0.60), (0.84, 0.10)),
                   ('C', (0.84, -0.30), (0.52, -0.64), (S, -0.64)),
                   ('L', (S, 0.50))])
    inner = shape([(S, 0.26),
                   ('C', (0.36, 0.42), (0.56, 0.38), (0.56, 0.08)),
                   ('C', (0.56, -0.20), (0.40, -0.44), (S, -0.44)),
                   ('L', (S, 0.26))])
    return union(stem, diff(outer, inner))


def dsharp(S_=1.0):
    """An X of four tapering arms, thin where they cross and wide at the corners, each arm
    cut square to its own direction."""
    c = S_ / 2
    arms = []
    for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        ux, uy = sx / math.sqrt(2), sy / math.sqrt(2)      # arm direction
        nx, ny = -uy, ux
        L = c * math.sqrt(2) * 0.92                         # centre to the arm's end
        w0, w1 = 0.07, 0.19                                 # half-widths at the centre and the end
        arms.append(poly([(-ux * 0.05 + nx * w0, -uy * 0.05 + ny * w0),
                          (ux * L + nx * w1, uy * L + ny * w1),
                          (ux * L - nx * w1, uy * L - ny * w1),
                          (-ux * 0.05 - nx * w0, -uy * 0.05 - ny * w0)]))
    p = union(*arms)
    x0, y0, x1, y1 = bounds(p)
    return move(p, -x0, -(y0 + y1) / 2)


def paren(H=2.6, left=True):
    outer = ellipse(0.62, 0, 0.62, H / 2)
    inner = ellipse(0.78, 0, 0.60, H / 2 - 0.02)
    p = inter(diff(outer, inner), rect(0, -H, 0.46, H))
    x0 = bounds(p)[0]
    p = move(p, -x0, 0)
    return p if left else mirror_x(p, bounds(p)[2] / 2)


sh = sharp()
add("accidentalSharp", sh, anchors={"cutOutNE": (0.76, 0.9), "cutOutNW": (0.2, 0.62),
                                    "cutOutSE": (0.76, -0.62), "cutOutSW": (0.2, -0.9)})
na = natural()
add("accidentalNatural", na, anchors={"cutOutNE": (0.15, 0.72), "cutOutSW": (0.51, -0.72)})
fl = flat()
add("accidentalFlat", fl, anchors={"cutOutNE": (0.2, 0.62), "cutOutSE": (0.6, -0.45)})
add("accidentalDoubleSharp", dsharp())
dfl = union(flat(), move(flat(), 0.82, 0))
add("accidentalDoubleFlat", dfl, anchors={"cutOutNE": (1.02, 0.62), "cutOutSE": (1.4, -0.45)})
add("accidentalTripleFlat", union(flat(), move(flat(), 0.82, 0), move(flat(), 1.64, 0)))
add("accidentalTripleSharp", union(sh, move(dsharp(), 1.08, 0)))
add("accidentalNaturalFlat", union(na, move(fl, 0.84, 0)))
add("accidentalNaturalSharp", union(na, move(sh, 0.84, 0)))
add("accidentalParensLeft", paren(2.6, True))
add("accidentalParensRight", paren(2.6, False))
add("accidentalBracketLeft", union(rect(0, -1.3, 0.20, 1.3), rect(0, 1.14, 0.44, 1.3), rect(0, -1.3, 0.44, -1.14)))
add("accidentalBracketRight", mirror_x(union(rect(0, -1.3, 0.20, 1.3), rect(0, 1.14, 0.44, 1.3), rect(0, -1.3, 0.44, -1.14)), 0.22))


# ---- text-sized accidentals for chord symbols / figured bass (sit on the baseline)
def text_acc(p, k=0.62, lift=None):
    q = scale(p, k)
    x0, y0, x1, y1 = bounds(q)
    return move(q, -x0 + 0.04, (-y0 - 0.02) if lift is None else lift)


import registry as _r
_ts = text_acc(sh, lift=-bounds(scale(sh, 0.62))[1] - 0.18)
_tf = text_acc(fl)
_tn = text_acc(na, lift=-bounds(scale(na, 0.62))[1] - 0.18)
_tds = text_acc(dsharp(), k=0.7, lift=0.18 + 0.35)
_tdf = text_acc(dfl)
for pre, names in (("csymAccidental", ("Flat", "Natural", "Sharp", "DoubleSharp", "DoubleFlat")),
                   ("figbass", ("Flat", "Natural", "Sharp", "DoubleSharp", "DoubleFlat"))):
    for nm, p in zip(names, (_tf, _tn, _ts, _tds, _tdf)):
        add(pre + nm, p, adv=bounds(p)[2] + 0.06)
