from kit import *
import math
from shapes import *
from registry import add
import registry
import g_dynamics as gd
from digits import diag, figs
import style
from style import K

S = style.STEM_SYM          # 0.30
s_ = style.thin(S)


# ---- segno: a big Modernoir S (narrow, straight spine, flat terminals), slash, two dots
def big_s(H=3.0, W=None):
    """The dynamics s drawn large: arcs and a straight spine in one stroke."""
    W = 0.45 * H if W is None else W
    p, _, _ = gd.L_s(w=W, H=H, wh=S * 0.95)
    return p


def segno():
    s = move(big_s(), 0.42, 0.04)
    slash = polyline([(0.06, 0.25), (2.14, 2.85)], s_ * 0.7)
    d1 = term(0.34, 1.20, 0.40)
    d2 = term(1.86, 1.86, 0.40)
    return union(s, slash, d1, d2)


add("segno", segno())


def coda(square=False):
    cx, cy = 1.9, 1.48
    if square:
        ring = diff(rect(cx - 0.95, cy - 1.2, cx + 0.95, cy + 1.2), rect(cx - 0.95 + S, cy - 1.2 + s_, cx + 0.95 - S, cy + 1.2 - s_))
    else:
        ring = diff(ellipse(cx, cy, 0.95, 1.22, 0, K), ellipse(cx, cy, 0.95 - S, 1.22 - s_, 0, K))
    lw = s_ * 0.72
    cross = union(rect(0, cy - lw / 2, 2 * cx, cy + lw / 2), rect(cx - lw / 2, -0.62, cx + lw / 2, 3.58))
    return union(ring, cross)


add("coda", coda())
add("codaSquare", coda(True))


# ---- ornaments: zigzags with the pen (diagonals a little heavier than the joins)
def zigzag(n_up, W, H=0.86, w=None):
    w = S * 0.8 if w is None else w
    xs = [i * W / (2 * n_up) for i in range(2 * n_up + 1)]
    pts = [(x, 0.06 if i % 2 == 0 else H) for i, x in enumerate(xs)]
    def ext(p, q, d=0.4):
        dx, dy = p[0] - q[0], p[1] - q[1]
        L = math.hypot(dx, dy)
        return (p[0] + dx / L * d, p[1] + dy / L * d)
    pts = [ext(pts[0], pts[1])] + pts[1:-1] + [ext(pts[-1], pts[-2])]
    p = polyline(pts, w)
    return inter(p, rect(0, -1, W, H + 1))


short_trill = zigzag(2, 2.4)
add("ornamentShortTrill", short_trill)
add("ornamentMordent", union(short_trill, rect(1.20 - s_ * 0.35, -0.32, 1.20 + s_ * 0.35, 1.28)))
add("ornamentTremblement", zigzag(3, 3.4))


def turn():
    s, _, _ = gd.L_s()
    x0, y0, x1, y1 = bounds(s)
    p = rotate(s, -90, (x0 + x1) / 2, (y0 + y1) / 2)
    x0, y0, x1, y1 = bounds(p)
    p = move(p, -x0, -y0)
    return scale(p, 1.9 / (bounds(p)[2]))


tn = turn()
add("ornamentTurn", tn)
add("ornamentTurnInverted", mirror_x(tn, bounds(tn)[2] / 2))
add("ornamentTurnSlash", union(tn, rect(bounds(tn)[2] / 2 - 0.06, -0.3, bounds(tn)[2] / 2 + 0.06, bounds(tn)[3] + 0.3)))


def trill():
    T, t = gd.T, gd.t
    XH = gd.XH
    # t: stem, crossbar, and a foot that turns right along the baseline, cut radially
    xs = 0.12
    R = 0.30
    foot = mirror_y(gd.hook(xs, 0.0, R, 32, ri=0.11))
    x0f, y0f, x1f, y1f = bounds(foot)
    tstem = rect(xs, R, xs + T, ASC_T)
    tbar = rect(0.0, XH - t, xs + T + 0.20, XH)
    tt = union(tstem, tbar, foot)
    rr, _, w_r = gd.L_r()
    gap = 0.12
    xr = max(xs + T + 0.20, bounds(tt)[2]) + gap
    p = union(tt, move(rr, xr, 0))
    return slant(p, gd.SLANT)


ASC_T = 1.34
tr = trill()
add("ornamentTrill", move(tr, -bounds(tr)[0], 0))

# ---- tremolos: parallel slanted bars
def trem(n, pitch=0.75, h=0.34, rise=0.40, W=1.2):
    bars = [parallelogram(-W / 2, W / 2, (i - (n - 1) / 2) * pitch, h, rise) for i in range(n)]
    return union(*bars)


for i in range(1, 6):
    add(f"tremolo{i}", trem(i))

# ---- brace: straight verticals with a chevron point in the middle
def brace():
    pts = [(0.36, 4.08), (0.20, 3.58), (0.20, 2.38), (0.02, 2.0), (0.20, 1.62), (0.20, 0.42), (0.36, -0.08)]
    p = polyline(pts, [0.08, 0.24, 0.20, 0.20, 0.24, 0.08])
    return inter(p, rect(-1, 0, 2, 4.0))


b = brace()
add("brace", move(b, -bounds(b)[0], 0))


def bracket_top():
    return poly([(0.0, 0.0), (0.5, 0.0), (1.86, 1.02), (1.86, 1.16), (0.0, 0.56)])


bt = bracket_top()
add("bracketTop", bt)
add("bracketBottom", mirror_y(bt))

# ---- chord-symbol marks (ring = true circle with the pen)
ring = diff(ellipse(0.8, 0.8, 0.8, 0.8), ellipse(0.8, 0.8, 0.8 - S * 0.9, 0.8 - s_ * 0.9))
add("csymDiminished", ring)
add("csymHalfDiminished", union(ring, polyline([(0.06, -0.02), (1.54, 1.62)], s_ * 0.6)))
import mono as _M
CS = 0.27                               # chord-symbol stroke, the same as the diminished ring


def outline(pts, w=CS):
    """A closed shape drawn as an outline of even width w (inside the given edge), mitred corners."""
    outer = _M.Polygon(pts)
    return _M.to_path(outer.difference(outer.buffer(-w, join_style='mitre', mitre_limit=10)))


tri = outline([(0.0, 0.0), (1.9, 0.0), (0.95, 1.7)])
add("csymMajorSeventh", tri)
add("csymAugmented", union(rect(0, 0.8 - CS / 2, 1.44, 0.8 + CS / 2), rect(0.72 - CS / 2, 0.08, 0.72 + CS / 2, 1.52)))
MB, MY = 0.31, 1.40       # minus: a little heavier, centred at the middle of the chord digits (Jost 7 = 700/1000 em)
add("csymMinor", rect(0, MY - MB / 2, 1.1, MY + MB / 2))

# Joel's own chord marks (not in SMuFL): diminished-major seventh = a diamond, minor-major
# seventh = the diamond with a full bar through it, running past both side corners.
DW, DH = 1.33, 1.70
dia = outline([(0.0, DH / 2), (DW / 2, 0.0), (DW, DH / 2), (DW / 2, DH)])
EXT, BAR = 0.30, 0.22
add("csymMinorMajorSeventh", union(move(dia, EXT, 0), rect(0.0, DH / 2 - BAR / 2, DW + 2 * EXT, DH / 2 + BAR / 2)))

# ---- repeat measure signs: slash with two dots
def repeat_bars(n):
    parts = []
    for i in range(n):
        parts.append(move(poly([(0, -1.0), (0.62, -1.0), (1.62, 1.0), (1.0, 1.0)]), i * 0.9, 0))
    W = 1.62 + (n - 1) * 0.9
    parts.append(term(0.28, 0.52, 0.40))
    parts.append(term(W - 0.28, -0.52, 0.40))
    return union(*parts)


add("repeat1Bar", repeat_bars(1))
add("repeat2Bars", repeat_bars(2))
add("repeat4Bars", repeat_bars(4))

# ---- ottava: figures and Modernoir letters, slanted like the dynamics
F = figs(H=1.6)
eight = move(F['8'], 0, 0.8)
fifteen = union(move(F['1'], 0, 0.8), move(F['5'], bounds(F['1'])[2] + 0.14, 0.8))
twentytwo = union(move(F['2'], 0, 0.8), move(F['2'], bounds(F['2'])[2] + 0.14, 0.8))


def ott(fig, text=None):
    f = slant(fig, gd.SLANT)
    if not text:
        return f
    l, _ = gd.word(text)
    l = scale(l, 0.8)
    x = bounds(f)[2] + 0.08
    return union(f, move(l, x - bounds(l)[0], 0))


add("ottava", ott(eight))
add("ottavaAlta", ott(eight, "va"))
add("ottavaBassa", ott(eight, "vb"))
add("ottavaBassaVb", ott(eight, "vb"))
add("ottavaBassaBa", ott(eight, "ba"))
add("quindicesima", ott(fifteen))
add("quindicesimaAlta", ott(fifteen, "ma"))
add("quindicesimaBassa", ott(fifteen, "mb"))
add("ventiduesima", ott(twentytwo))
add("ventiduesimaAlta", ott(twentytwo, "ma"))
add("ventiduesimaBassa", ott(twentytwo, "mb"))
