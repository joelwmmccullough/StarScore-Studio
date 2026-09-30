"""Dynamics: Modernoir-proportioned letters, slanted 10 degrees.
x-height 1.06 sp, baseline y=0. Widths, stem and counters follow style.py."""
from kit import *
from shapes import *
from registry import add
from digits import diag
import style
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


def quarter_ring(cx, cy, r, wall, thin_, left=None):
    """Quarter ring from the top of a stem at (cx, cy+r) curving right and down to (cx+r, cy):
    cut flat where the curve turns vertical, as Modernoir's r and f. `left` = how far left of
    the centre line to keep (default: the stem's left edge)."""
    left = T * 0.5 if left is None else left
    q = bowl(cx, cy, r, r, wall, thin_)
    return inter(q, rect(cx - left, cy, cx + r + 1, cy + r + 1))


def L_r():
    w = W('r')
    r = w - T * 0.5
    arm = quarter_ring(T * 0.5, XH - r, r, T * 0.9, t * 0.9)
    return union(rect(0, 0, T, XH), arm), 0.0, w


def L_f():
    w = W('f')
    xs = 0.05
    r = w - xs - T * 0.5
    cyt = ASC - r
    stem = rect(xs, DESC + r, xs + T, cyt)
    hook = quarter_ring(xs + T * 0.5, cyt, r, r, r * 0.9)
    tail = rotate(hook, 180, xs + T / 2, (ASC + DESC) / 2)
    bar = rect(0, XH - t, w, XH)
    return union(stem, hook, tail, bar), 0.0, w


def L_s():
    w = W('s')
    Tw = T * 0.84
    ry = XH / 4
    cyu = XH - ry
    up = inter(bowl(w / 2, cyu, w / 2, ry, Tw, t), rect(-5, cyu, 5, 5))
    up = diff(up, rect(w / 2, -5, 5, cyu + 0.10))                 # top-right terminal, flat
    spine = diag(0.0, Tw, cyu, w - Tw, w, ry)
    return union(up, spine, rotate(up, 180, w / 2, XH / 2)), 0.0, w


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
