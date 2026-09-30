"""Dynamics: condensed deco letters, built like the figures and slanted 12 degrees.
x-height 1.0 sp, baseline y=0, ascender 1.72, descender -0.62."""
from kit import *
from shapes import *
from registry import add
from digits import diag

KL = 0.74  # squarish letter bowls (Modernoir's D-shaped bowls)


def bowl(cx, cy, rx, ry, T_, t_):
    return diff(ellipse(cx, cy, rx, ry, 0, KL), ellipse(cx, cy, rx - T_, ry - t_, 0, KL))


T = 0.25     # vertical stroke
t = 0.10     # horizontal stroke
XH = 1.06
ASC = 1.62
DESC = -0.48
SLANT = 10
GAP = 0.12   # gap between letter bodies


def arch(xl, xr, top=XH, ry=0.40):
    """n-style arch whose left wall sits on the stem at xl, with its right leg."""
    cx = (xl + xr) / 2
    rx = (xr - xl) / 2
    cy = top - ry
    a = bowl(cx, cy, rx, ry, T, t)
    a = inter(a, rect(-5, cy, 5, 5))
    leg = rect(xr - T, 0, xr, cy + 0.01)
    return union(a, leg)


C = 0.27     # counter width


def L_p():
    stem = rect(0, DESC, T, XH)
    W = 2 * T + C + 0.02
    b = bowl(W / 2, XH / 2, W / 2, XH / 2, T, t)
    return union(stem, b), 0.0, W


def L_m():
    P = T + C - 0.02
    s0 = rect(0, 0, T, XH)
    return union(s0, arch(0.0, P + T), arch(P, 2 * P + T)), 0.0, 2 * P + T


def L_n():
    W = 2 * T + C
    return union(rect(0, 0, T, XH), arch(0.0, W)), 0.0, W


def L_r():
    W = 2 * T + C - 0.02
    ry = 0.40
    cy = XH - ry
    s0 = rect(0, 0, T, XH)
    a = inter(bowl(W / 2, cy, W / 2, ry, T, t), rect(-5, cy, 5, 5))
    a = diff(a, rect(W / 2 + 0.02, -5, 5, cy + 0.20))     # short drop terminal
    return union(s0, a), 0.0, W


def L_f():
    xs = 0.26
    W = 2 * T + 0.20
    ry = 0.34
    cyt = ASC - ry
    stem = rect(xs, DESC + ry, xs + T, cyt + 0.01)
    hook = inter(bowl(xs + W / 2, cyt, W / 2, ry, T, t), rect(-5, cyt, 5, 5))
    hook = diff(hook, rect(xs + W / 2 + 0.02, -5, 5, cyt + 0.16))   # drop terminal, flat cut
    tail = rotate(hook, 180, xs + T / 2, (ASC + DESC) / 2)
    bar = rect(xs - 0.24, XH - t * 1.3, xs + T + 0.24, XH)
    return union(stem, hook, tail, bar), xs - 0.02, xs + T + 0.02


def L_s():
    W = 2 * T + C - 0.04
    ry = 0.27
    cyu = XH - ry
    up = inter(bowl(W / 2, cyu, W / 2, ry, T, t), rect(-5, cyu, 5, 5))
    up = diff(up, rect(W / 2 + 0.02, -5, 5, cyu + 0.12))                    # top-right terminal
    D = T * 1.3
    spine = diag(0.0, D, cyu + 0.001, W - D, W, ry - 0.001)
    return union(up, spine, rotate(up, 180, W / 2, XH / 2)), 0.0, W


def L_z():
    W = 2 * T + C - 0.06
    bt = t * 1.3
    D = T * 1.2
    top = rect(0.03, XH - bt, W, XH)
    bot = rect(0.0, 0.0, W - 0.03, bt)
    d = diag(W - D, W, XH - bt + 0.001, 0.0, D, bt - 0.001)
    return union(top, bot, d), 0.0, W


LETTERS = {'p': L_p, 'm': L_m, 'n': L_n, 'r': L_r, 'f': L_f, 's': L_s, 'z': L_z}
_c = {}


def letter(ch):
    if ch not in _c:
        _c[ch] = LETTERS[ch]()
    return _c[ch]


def word(s):
    parts = []
    x = 0.0
    first = True
    for ch in s:
        p, bl, br = letter(ch)
        if first:
            dx = -bl
            first = False
        else:
            g = GAP
            if prev == 'f' and ch == 'f':
                g = GAP + 0.02
            dx = x + g - bl
        parts.append(move(p, dx, 0))
        x = dx + br
        prev = ch
    p = union(*parts)
    p = slant(p, SLANT, 0.0)
    return p, x


def dyn(name, s):
    p, body_right = word(s)
    x0, y0, x1, y1 = bounds(p)
    # optical centre: centre of the letter bodies at half x-height (after slant)
    oc = (body_right / 2 + math.tan(math.radians(SLANT)) * XH / 2)
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
