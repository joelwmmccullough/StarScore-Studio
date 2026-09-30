from kit import *
from shapes import *
from registry import add
import registry
import g_dynamics as gd
from digits import diag, figs

# ---- segno: a large deco S (straight spine), slash, two dots
def big_s(H=2.9, W=1.46, T=0.40, t=0.13):
    ry = H * 0.25
    cyu = H - ry
    KL = 0.70
    def bowl(cx, cy, rx, ry_):
        return diff(ellipse(cx, cy, rx, ry_, 0, KL), ellipse(cx, cy, rx - T, ry_ - t, 0, KL))
    up = inter(bowl(W / 2, cyu, W / 2, ry), rect(-5, cyu, 5, 9))
    up = diff(up, rect(W / 2 + 0.02, -5, 5, cyu + 0.18))
    D = T * 1.3
    spine = diag(0.0, D, cyu + 0.001, W - D, W, ry - 0.001)
    return union(up, spine, rotate(up, 180, W / 2, H / 2))


def segno():
    s = move(big_s(), 0.38, 0.05)
    slash = inter(polyline([(0.0, 0.25), (2.2, 2.85)], 0.14), rect(0, 0, 2.2, 3.1))
    d1 = term(0.36, 1.22, 0.40)
    d2 = term(1.86, 1.86, 0.40)
    return union(s, slash, d1, d2)


add("segno", segno())


def coda(square=False):
    cx, cy = 1.9, 1.48
    if square:
        ring = diff(rect(cx - 0.95, cy - 1.2, cx + 0.95, cy + 1.2), rect(cx - 0.95 + 0.30, cy - 1.2 + 0.12, cx + 0.95 - 0.30, cy + 1.2 - 0.12))
    else:
        ring = diff(ellipse(cx, cy, 0.95, 1.22, 0, 0.60), ellipse(cx, cy, 0.95 - 0.30, 1.22 - 0.11, 0, 0.60))
    cross = union(rect(0, cy - 0.07, 2 * cx, cy + 0.07), rect(cx - 0.07, -0.62, cx + 0.07, 3.58))
    return union(ring, cross)


add("coda", coda())
add("codaSquare", coda(True))


# ---- ornaments
def zigzag(n_up, W, H=0.86, thin=0.12, thick=0.30):
    pts = []
    xs = [i * W / (2 * n_up) for i in range(2 * n_up + 1)]
    for i, x in enumerate(xs):
        pts.append((x, 0.08 if i % 2 == 0 else H))
    ws = [thin if i % 2 == 0 else thick for i in range(len(pts) - 1)]
    p = polyline(pts, ws)
    return inter(p, rect(0, -0.2, W, H + 0.12))


short_trill = zigzag(2, 2.4)
add("ornamentShortTrill", short_trill)
add("ornamentMordent", union(short_trill, rect(1.14, -0.32, 1.26, 1.28)))
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
    T, t = 0.27, 0.11
    XH = 1.05
    # t: stem with crossbar and a flat foot turning right
    tstem = rect(0.16, 0.22, 0.16 + T, 1.52)
    tbar = rect(0.0, XH - t * 1.3, 0.76, XH)
    foot = inter(diff(ellipse(0.52, 0.36, 0.36, 0.36, 0, 0.74), ellipse(0.52, 0.36, 0.36 - T, 0.36 - t, 0, 0.74)),
                 rect(0.16, -1, 0.70, 0.36))
    tt = union(tstem, tbar, foot)
    r, _, _ = gd.L_r()
    p = union(tt, move(r, 0.84, 0))
    return slant(p, gd.SLANT)


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
    pts = [(0.30, 4.06), (0.17, 3.60), (0.17, 2.36), (0.02, 2.0), (0.17, 1.64), (0.17, 0.40), (0.30, -0.06)]
    p = polyline(pts, [0.07, 0.15, 0.13, 0.13, 0.15, 0.07])
    return inter(p, rect(-1, 0, 2, 4.0))


b = brace()
add("brace", move(b, -bounds(b)[0], 0))


def bracket_top():
    p = poly([(0.0, 0.0), (0.5, 0.0), (1.86, 1.02), (1.86, 1.16), (0.0, 0.56)])
    return p


bt = bracket_top()
add("bracketTop", bt)
add("bracketBottom", mirror_y(bt))

# ---- chord-symbol marks
add("csymDiminished", diff(ellipse(0.8, 0.8, 0.8, 0.8), ellipse(0.8, 0.8, 0.8 - 0.26, 0.8 - 0.12)))
add("csymHalfDiminished", union(diff(ellipse(0.8, 0.8, 0.8, 0.8), ellipse(0.8, 0.8, 0.8 - 0.26, 0.8 - 0.12)),
                                inter(polyline([(0.0, -0.1), (1.6, 1.7)], 0.13), rect(-0.1, -0.1, 1.7, 1.7))))
tri = diff(poly([(0.0, 0.0), (1.9, 0.0), (0.95, 1.7)]), poly([(0.36, 0.13), (1.54, 0.13), (0.95, 1.20)]))
add("csymMajorSeventh", tri)
add("csymAugmented", union(rect(0, 0.72, 1.44, 0.88), rect(0.62, 0.0, 0.82, 1.6)))
add("csymMinor", rect(0, 0.72, 1.1, 0.88))

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

# ---- ottava: small slanted figures and letters
F = figs(H=1.6, T=0.36, t=0.13, W=1.04)
eight = move(F['8'], 0, 0.8)
fifteen = union(move(F['1'], 0, 0.8), move(F['5'], bounds(F['1'])[2] + 0.14, 0.8))
twentytwo = union(move(F['2'], 0, 0.8), move(F['2'], bounds(F['2'])[2] + 0.14, 0.8))


def small_letters(s, k=0.8):
    p, w = gd.word(s)
    return scale(p, k), w * k


def letter_v():
    return union(diag(0.0, 0.26, gd.XH, 0.22, 0.44, 0.0), diag(0.52, 0.72, gd.XH, 0.22, 0.44, 0.0))


def letter_a():
    W = 2 * gd.T + gd.C
    b = gd.bowl(W / 2, gd.XH / 2, W / 2, gd.XH / 2, gd.T, gd.t)
    return union(b, rect(W - gd.T, 0, W, gd.XH))


def letter_b():
    p, _, W = gd.L_p()
    return mirror_y(p, gd.XH / 2)


gd.LETTERS['v'] = lambda: (letter_v(), 0.0, 0.72)
gd.LETTERS['a'] = lambda: (letter_a(), 0.0, 2 * gd.T + gd.C)
gd.LETTERS['b'] = lambda: (letter_b(), 0.0, 2 * gd.T + gd.C + 0.02)


def ott(fig, text=None):
    f = slant(fig, gd.SLANT)
    if not text:
        return f
    l, _ = small_letters(text)
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
