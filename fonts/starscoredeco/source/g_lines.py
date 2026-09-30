"""Tiling line glyphs (trill/arpeggio/glissando wiggles), pedal marks, metronome notes."""
from kit import *
from shapes import *
from registry import add
import registry
import g_dynamics as gd


def zig_tile(period, y0, y1, w_up, w_down, eps=0.006):
    """One period of a zigzag that tiles seamlessly: advance = period."""
    h = period / 2
    pts = [(-h, y1), (0, y0), (h, y1), (period, y0), (period + h, y1)]
    ws = [w_down, w_up, w_down, w_up]
    p = polyline(pts, ws)
    return inter(p, rect(-eps, -5, period + eps, 5))


wt = zig_tile(0.96, 0.40, 0.84, 0.10, 0.22)
add("wiggleTrill", wt, adv=0.96, anchors={"repeatOffset": (0.96, 0)})
wa = zig_tile(1.0, 0.04, 0.46, 0.09, 0.18)
add("wiggleArpeggiatoUp", wa, adv=1.0, anchors={"repeatOffset": (1.0, 0)})
add("wiggleArpeggiatoDown", mirror_y(wa, 0.25), adv=1.0, anchors={"repeatOffset": (1.0, 0)})
wg = zig_tile(0.96, 0.04, 0.44, 0.09, 0.16)
add("wiggleGlissando", wg, adv=0.96, anchors={"repeatOffset": (0.96, 0)})


def arrow_tile(up=True):
    base = zig_tile(1.0, 0.04, 0.46, 0.09, 0.18)
    head = poly([(1.0, -0.30), (2.06, 0.25), (1.0, 0.80)])
    p = union(base, head)
    return p if up else mirror_y(p, 0.25)


add("wiggleArpeggiatoUpArrow", arrow_tile(True), adv=2.06, anchors={"repeatOffset": (2.06, 0)})
add("wiggleArpeggiatoDownArrow", arrow_tile(False), adv=2.06, anchors={"repeatOffset": (2.06, 0)})


# ---- pedal marks
def letter_P(T=gd.T, t=gd.t):
    H = 1.5
    stem = rect(0, 0, T, H)
    W = 2 * T + gd.C + 0.06
    ry = 0.42
    b = gd.bowl(W / 2, H - ry, W / 2, ry, T, t)
    joins = union(rect(0, H - t, W / 2, H), rect(0, H - 2 * ry, W / 2, H - 2 * ry + t))
    return union(stem, b, joins), 0.0, W


def letter_e():
    T, t = gd.T, gd.t
    W = 2 * T + gd.C
    XH = gd.XH
    ring_ = gd.bowl(W / 2, XH / 2, W / 2, XH / 2, T, t)
    bar_ = rect(0.02, XH * 0.52, W - 0.02, XH * 0.52 + t * 1.2)
    p = union(ring_, bar_)
    p = diff(p, rect(W / 2 + 0.02, 0.18, 5, XH * 0.52 - 0.001))   # open lower right, flat terminal
    return p, 0.0, W


def letter_d():
    T, t = gd.T, gd.t
    W = 2 * T + gd.C + 0.02
    b = gd.bowl(W / 2, gd.XH / 2, W / 2, gd.XH / 2, T, t)
    return union(b, rect(W - T, 0, W, gd.ASC), gd.shoulders(W / 2, W)), 0.0, W


gd.LETTERS['P'] = letter_P
gd.LETTERS['e'] = letter_e
gd.LETTERS['d'] = letter_d


def ped():
    p, w = gd.word("Ped")
    p = scale(p, 1.25)
    x1 = bounds(p)[2]
    dot = term(x1 + 0.22, 0.14, 0.34)
    return union(p, dot)


pd = ped()
add("keyboardPedalPed", move(pd, -bounds(pd)[0], 0))
add("keyboardPedalP", scale(slant(letter_P()[0], gd.SLANT), 1.25))
add("keyboardPedalDot", term(0.17, 0.17, 0.34))


def sunburst():
    c = 0.9
    ringp = diff(ellipse(c, c, 0.36, 0.36), ellipse(c, c, 0.20, 0.20))
    rays = []
    for i in range(8):
        a = math.radians(i * 45 + 22.5)
        p0 = (c + 0.46 * math.cos(a), c + 0.46 * math.sin(a))
        p1 = (c + 0.90 * math.cos(a), c + 0.90 * math.sin(a))
        # wedge ray: wider at the outside
        nx, ny = -math.sin(a), math.cos(a)
        rays.append(poly([(p0[0] + nx * 0.04, p0[1] + ny * 0.04), (p1[0] + nx * 0.11, p1[1] + ny * 0.11),
                          (p1[0] - nx * 0.11, p1[1] - ny * 0.11), (p0[0] - nx * 0.04, p0[1] - ny * 0.04)]))
    return union(ringp, *rays)


add("keyboardPedalUp", sunburst())


# ---- metronome notes (for tempo marks)
G = registry.G
STEM = 0.12
STEM_TOP = 2.75


def met(head, flag=None, stem=True):
    h = G[head]["path"]
    parts = [h]
    if stem:
        a = G[head]["anchors"]["stemUpSE"]
        sx = a[0]
        top = STEM_TOP
        if flag:
            # like MuseScore: lengthen the stem to the flag's attachment point
            ext = G[flag]["anchors"].get("stemUpNW", (0, 0))[1]
            top = STEM_TOP + max(0.0, ext)
            parts.append(move(G[flag]["path"], sx - STEM, STEM_TOP))
        parts.append(rect(sx - STEM, a[1], sx, top + 0.01))
    return union(*parts)


add("metNoteQuarterUp", met("noteheadBlack"))
add("metNoteHalfUp", met("noteheadHalf"))
add("metNoteWhole", met("noteheadWhole", stem=False))
add("metNote8thUp", met("noteheadBlack", "flag8thUp"))
add("metNote16thUp", met("noteheadBlack", "flag16thUp"))
add("metNote32ndUp", met("noteheadBlack", "flag32ndUp"))
add("metAugmentationDot", G["augmentationDot"]["path"])
