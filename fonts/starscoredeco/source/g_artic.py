from kit import *
import style
from style import K
import math
from shapes import *
from registry import add

DOT = 0.36


def dot_at(cx, cy, s=DOT):
    return term(cx, cy, s)


def accent():
    p = polyline([(-0.2, 0.90), (1.32, 0.45), (-0.2, 0.0)], [0.22, 0.22])
    return inter(p, rect(0.0, -0.1, 2, 1.0))


def marcato():
    p = polyline([(-0.08, -0.25), (0.47, 1.02), (1.02, -0.25)], [0.30, 0.24])
    return inter(p, rect(0, 0, 0.94, 1.2))


def tenuto():
    return rect(0, 0, 1.30, 0.22)


def wedge():
    return poly([(0.0, 1.10), (0.38, 1.10), (0.19, 0.0)])


def stacc():
    return dot_at(DOT / 2 + 0.02, DOT / 2 + 0.02)


def above_below(name, p):
    x0, y0, x1, y1 = bounds(p)
    p = move(p, -x0, -y0)
    add(name + "Above", p)
    add(name + "Below", mirror_y(p))


def stack(*parts, gap=0.26):
    """Stack parts bottom-to-top, centred horizontally."""
    ws = [bounds(p)[2] - bounds(p)[0] for p in parts]
    W = max(ws)
    y = 0
    out = []
    for p, w in zip(parts, ws):
        x0, y0, x1, y1 = bounds(p)
        out.append(move(p, (W - w) / 2 - x0, y - y0))
        y += (y1 - y0) + gap
    return union(*out)


above_below("articAccent", accent())
above_below("articStaccato", stacc())
above_below("articTenuto", tenuto())
above_below("articStaccatissimo", wedge())
above_below("articStaccatissimoWedge", wedge())
above_below("articMarcato", marcato())
above_below("articAccentStaccato", stack(stacc(), accent()))
above_below("articTenutoStaccato", stack(stacc(), tenuto()))
above_below("articMarcatoStaccato", stack(stacc(), marcato()))
above_below("articMarcatoTenuto", stack(tenuto(), marcato()))
above_below("articTenutoAccent", stack(tenuto(), accent()))
above_below("articStaccatissimoStroke", rect(0, 0, 0.22, 0.9))


def chevron(W, H, th):
    ang = math.atan2(H, W / 2)
    a = th / math.sin(ang)          # horizontal width of each arm at the base
    b = th / math.cos(ang)          # vertical drop of the inner apex
    return poly([(0, 0), (W / 2, H), (W, 0), (W - a, 0), (W / 2, H - b), (a, 0)])


# ---- fermatas: a squarish arch, thick at the crown, cut flat at the base
def fermata(kind="normal"):
    W = 2.36
    if kind == "normal":
        outer = ellipse(W / 2, 0, W / 2, 1.26, 0, K)
        inner = ellipse(W / 2, 0, W / 2 - 0.30, 1.26 - 0.24, 0, K)
        arc_ = inter(diff(outer, inner), rect(-1, 0, W + 1, 2))
    elif kind == "short":   # pointed arch (chevron) with mitred apex, flat feet
        arc_ = chevron(W, 1.26, 0.24)
    elif kind == "long":    # square arch
        arc_ = union(rect(0, 0, 0.26, 1.20), rect(W - 0.26, 0, W, 1.20), rect(0, 0.98, W, 1.20))
    elif kind == "veryLong":
        arc_ = union(rect(0, 0, 0.24, 1.20), rect(W - 0.24, 0, W, 1.20), rect(0, 1.0, W, 1.20),
                     rect(0.40, 0, 0.62, 0.76), rect(W - 0.62, 0, W - 0.40, 0.76), rect(0.40, 0.58, W - 0.40, 0.76))
    elif kind == "veryShort":
        arc_ = union(chevron(W, 1.34, 0.22), move(chevron(W - 1.0, 0.86, 0.19), 0.50, 0))
    d = dot_at(W / 2, 0.26 if kind in ('short', 'veryShort') else 0.30, 0.36 if kind in ('short', 'veryShort') else 0.40)
    return union(arc_, d)


for kind, nm in (("normal", "fermata"), ("short", "fermataShort"), ("long", "fermataLong"),
                 ("veryLong", "fermataVeryLong"), ("veryShort", "fermataVeryShort")):
    f = fermata(kind)
    add(nm + "Above", f)
    add(nm + "Below", mirror_y(f))

# ---- breath marks and caesuras
comma = stroke([(0.26, 0.72), ('C', (0.40, 0.64), (0.47, 0.44), (0.40, 0.27)),
                ('C', (0.33, 0.12), (0.21, 0.04), (0.06, 0.0))], style.pen(0.24),
               ball0=(0.46, 0.50), wfun=lambda s: 1.0 - 0.45 * s)
add("breathMarkComma", move(comma, -bounds(comma)[0], -bounds(comma)[1]))
tick = polyline([(0.0, 0.9), (0.42, 0.0), (1.2, 1.9)], [0.20, 0.26])
add("breathMarkTick", move(tick, -bounds(tick)[0], -bounds(tick)[1]))
cz = union(polyline([(0.0, 0.0), (0.62, 2.1)], 0.24), polyline([(0.62, 0.0), (1.24, 2.1)], 0.24))
cz = inter(cz, rect(-1, 0, 3, 2.1))
add("caesura", move(cz, -bounds(cz)[0], 0))
czt = inter(union(polyline([(0.0, 0.0), (0.72, 2.1)], 0.36), polyline([(0.86, 0.0), (1.58, 2.1)], 0.36)), rect(-1, 0, 3, 2.1))
add("caesuraThick", move(czt, -bounds(czt)[0], 0))
czs = inter(union(polyline([(0.0, 0.0), (0.40, 1.3)], 0.22), polyline([(0.5, 0.0), (0.9, 1.3)], 0.22)), rect(-1, 0, 3, 1.3))
add("caesuraShort", move(czs, -bounds(czs)[0], 0))

# ---- dots
R_DOT = 0.38 * 0.58
add("augmentationDot", dot_at(R_DOT, 0.0, 0.38))
add("repeatDot", dot_at(R_DOT, 0.0, 0.38))
add("repeatDots", union(dot_at(R_DOT, 1.5, 0.38), dot_at(R_DOT, 2.5, 0.38)))
