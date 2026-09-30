"""Dynamics: Modernoir-proportioned letters, slanted 10 degrees.
x-height 1.06 sp, baseline y=0. Widths, stem and counters follow style.py."""
from kit import *
from shapes import *
from registry import add
from digits import diag
import style
import math
from style import K

XH = 1.06
ASC = 1.40 * XH        # Modernoir ascender 1.33 x-height, f 1.35
DESC = -0.40 * XH
SLANT = 10
GAP = 0.11
T = style.STEM_TEXT    # 0.25
t = style.thin(T)      # 0.205
C = 0.20               # counter width of a bowl (p, b, d, a, o)


def W(ch):
    return style.LC_W[ch] * XH


def bowl(cx, cy, rx, ry, T_=T, t_=t):
    return diff(ellipse(cx, cy, rx, ry, 0, K), ellipse(cx, cy, rx - T_, ry - t_, 0, K))


def oval_bowl(x0, w, h=XH, cy=None):
    """A full elliptical bowl filling x0..x0+w, height h, with a narrow oval counter."""
    cy = h / 2 if cy is None else cy
    return bowl(x0 + w / 2, cy, w / 2, h / 2)


def arch(xl, xr, top=XH):
    """Semicircular arch (Modernoir n): full round top between xl and xr, right leg down."""
    r = (xr - xl) / 2
    cy = top - r
    a = inter(bowl(xl + r, cy, r, r), rect(-5, cy, 5, 5))
    return union(a, rect(xr - T, 0, xr, cy + 0.01))


# ---------------------------------------------------------------- letters
def L_p():
    w = W('p')
    stem = rect(0, DESC, T, XH)
    b = oval_bowl(0, w)
    joins = union(rect(0, XH - t, w / 2, XH), rect(0, 0, w / 2, t))
    return union(stem, b, joins), 0.0, w


def L_b():
    w = W('b')
    b = oval_bowl(0, w)
    return union(b, rect(0, 0, T, ASC), rect(0, XH - t, w / 2, XH), rect(0, 0, w / 2, t)), 0.0, w


def L_d():
    w = W('d')
    b = oval_bowl(0, w)
    return union(b, rect(w - T, 0, w, ASC), rect(w / 2, XH - t, w, XH), rect(w / 2, 0, w, t)), 0.0, w


def L_a():
    w = W('a')
    b = oval_bowl(0, w)
    return union(b, rect(w - T, 0, w, XH), rect(w / 2, XH - t, w, XH), rect(w / 2, 0, w, t)), 0.0, w


def L_n():
    w = W('n')
    return union(rect(0, 0, T, XH), arch(0, w), rect(0, XH - t, w / 2, XH)), 0.0, w


def L_m():
    w = W('m')
    P = (w - T) / 2          # pitch between stems
    return union(rect(0, 0, T, XH), arch(0, P + T), arch(P, w),
                 rect(0, XH - t, (P + T) / 2, XH), rect(P, XH - t, P + (P + T) / 2, XH)), 0.0, w


def hook(x0, top, R, a_end, ri=0.12):
    """A stem (left edge x0) turning over the top into a hook that ends in a radial cut at
    a_end degrees. Outer curve: circle of radius R touching the stem's left edge and the top
    line. Inner curve: a smaller circle touching the stem's right edge and sitting t below the
    top, so the stroke stays T thick on the side and t thick on top."""
    cx, cy = x0 + R, top - R
    outer = ellipse(cx, cy, R, R)
    icx, icy = x0 + T + ri, top - t - ri
    inner = ellipse(icx, icy, ri, ri)
    # below the inner circle's centre the counter is open (a vertical slot as wide as 2*ri)
    inner = union(inner, rect(icx - ri, icy - 5, icx + ri + 5, icy))
    far = 5
    keep = poly([(cx, cy)] + [(cx + far * math.cos(math.radians(a)), cy + far * math.sin(math.radians(a)))
                              for a in range(int(a_end), 271, 5)])
    return diff(inter(outer, keep), inner)


def L_r():
    R = W('n') / 2                               # same outer radius as the n and m arches
    arm = hook(0.0, XH, R, 40, ri=0.10)
    x1 = bounds(arm)[2]
    return union(rect(0, 0, T, XH), arm, rect(0, XH - t, R, XH)), 0.0, x1


def L_f():
    xs = 0.0
    R = 0.29
    cyt = ASC - R
    top = hook(xs, ASC, R, 28)
    tail = rotate(top, 180, xs + T / 2, (ASC + DESC) / 2)
    stem = rect(xs, DESC + R, xs + T, ASC - R)
    p = union(stem, top, tail, rect(xs - 0.10, XH - t, xs + T + 0.24, XH))
    x0 = bounds(p)[0]
    p = move(p, -x0, 0)
    return p, 0.0, bounds(p)[2]


def L_s(w=None, H=XH, wh=0.19, k=1.22, a_top=32, a_bot=-148):
    """Modernoir s: two elliptic arcs joined by a straight spine tangent to both. Drawn as one
    even stroke of width wh on a narrower skeleton, then widened by k, so the sides come out
    k times heavier than the tops and bottoms (the letter's vertical stress)."""
    import mono as Mo
    from shapely import affinity
    w = W('s') if w is None else w
    wn = w / k
    e = wh / 2
    ryu = (H - wh) * 0.205
    ryl = (H - wh) * 0.225
    Eu = (wn / 2 + 0.01, H - e - ryu, wn / 2 - e - 0.01, ryu)
    El = (wn / 2, e + ryl, wn / 2 - e, ryl)
    for which in (0, 1):
        p1, p2 = Mo.common_tangent(Eu, El, internal=True, which=which)
        if p1[0] < Eu[0]:
            break
    a1 = Mo.ell_angle(*Eu, p1) % 360
    a2 = Mo.ell_angle(*El, p2)
    if a2 > 90:
        a2 -= 360
    sk = Mo.chain(Mo.arc(*Eu, a_top, a1), Mo.line(p1, p2), Mo.arc(*El, a2, a_bot))
    g = affinity.scale(Mo.buf(sk, wh, join='round'), k, 1.0, origin=(0, 0))
    return Mo.to_path(g), 0.0, w


def L_z():
    w = W('z')
    D = T * 1.15
    top = rect(0.02, XH - t, w, XH)
    bot = rect(0.0, 0.0, w - 0.02, t)
    d = diag(w - D, w, XH - t, 0.0, D, t)
    return union(top, bot, d), 0.0, w


def L_e():
    w = W('e')
    b = oval_bowl(0, w)
    bar = rect(0.02, XH * 0.50, w - 0.02, XH * 0.50 + t)
    p = union(b, bar)
    p = diff(p, rect(w / 2, 0.16, 5, XH * 0.50))                # open lower right, flat cut
    return p, 0.0, w


def L_v():
    w = 0.66 * XH
    D = T * 1.1
    return union(diag(0.0, D, XH, (w - D) / 2, (w + D) / 2, 0.0),
                 diag(w - D, w, XH, (w - D) / 2, (w + D) / 2, 0.0)), 0.0, w


LETTERS = {'p': L_p, 'm': L_m, 'n': L_n, 'r': L_r, 'f': L_f, 's': L_s, 'z': L_z,
           'e': L_e, 'a': L_a, 'b': L_b, 'd': L_d, 'v': L_v}
_c = {}


def letter(ch):
    if ch not in _c:
        _c[ch] = LETTERS[ch]()
    return _c[ch]


def word(s):
    parts = []
    x = 0.0
    for i, ch in enumerate(s):
        p, bl, br = letter(ch)
        g = 0.0 if (i and s[i - 1] == 'f' and ch == 'f') else GAP     # ff: one continuous crossbar
        dx = -bl if i == 0 else x + g - bl
        parts.append(move(p, dx, 0))
        x = dx + br
    return slant(union(*parts), SLANT, 0.0), x


def dyn(name, s):
    p, body_right = word(s)
    x0, y0, x1, y1 = bounds(p)
    oc = body_right / 2 + math.tan(math.radians(SLANT)) * XH / 2
    add(name, p, adv=x1 + 0.1, anchors={"opticalCenter": (oc, 0.0)})


for name, s in [
    ("dynamicPiano", "p"), ("dynamicMezzo", "m"), ("dynamicForte", "f"),
    ("dynamicRinforzando", "r"), ("dynamicSforzando", "s"), ("dynamicZ", "z"), ("dynamicNiente", "n"),
    ("dynamicPPPPPP", "pppppp"), ("dynamicPPPPP", "ppppp"), ("dynamicPPPP", "pppp"), ("dynamicPPP", "ppp"),
    ("dynamicPP", "pp"), ("dynamicMP", "mp"), ("dynamicMF", "mf"), ("dynamicPF", "pf"),
    ("dynamicFF", "ff"), ("dynamicFFF", "fff"), ("dynamicFFFF", "ffff"), ("dynamicFFFFF", "fffff"),
    ("dynamicFFFFFF", "ffffff"), ("dynamicFortePiano", "fp"), ("dynamicForzando", "fz"),
    ("dynamicSforzando1", "sf"), ("dynamicSforzandoPiano", "sfp"), ("dynamicSforzandoPianissimo", "sfpp"),
    ("dynamicSforzato", "sfz"), ("dynamicSforzatoPiano", "sfzp"), ("dynamicSforzatoFF", "sffz"),
    ("dynamicRinforzando1", "rf"), ("dynamicRinforzando2", "rfz"), ("dynamicNienteForHairpin", "n"),
]:
    dyn(name, s)
