from kit import *
from shapes import *
from registry import add

# ---- proportions
HEAD_W = 1.26      # black / half notehead width (Bravura 1.18, Leland 1.30)
HEAD_H = 1.0
TILT = 24          # degrees
SQ = 0.565        # near-true ellipse
WHOLE_W = 1.62
STEM_T = 0.12


def black():
    p, rx, ry = fit_ellipse_box(HEAD_W, HEAD_H, TILT, SQ)
    return p


def half():
    p = black()
    # counter: a narrow slot set steeper than the head, so the ring is thick
    # at left and right and thin top and bottom (Modernoir "o").
    c = stadium(HEAD_W / 2, 0, 0.84, 0.30, TILT + 30)
    return diff(p, c)


def whole():
    p, _, _ = fit_ellipse_box(WHOLE_W, HEAD_H, 0, 0.64)
    c = stadium(WHOLE_W / 2, 0, 0.86, 0.40, 68)
    return diff(p, c)


def stem_anchors(p):
    x0, y0, x1, y1 = bounds(p)
    yr = edge_y(p, 'right')
    # attach a little below the rightmost point so the stem joins ink cleanly
    ya = max(min(yr - 0.02, 0.2), 0.12)
    return {"stemUpSE": (x1, ya), "stemDownNW": (0.0, -ya)}


b = black()
add("noteheadBlack", b, anchors={**stem_anchors(b), **cutouts(b)})
h = half()
add("noteheadHalf", h, anchors={**stem_anchors(h), **cutouts(h)})
w = whole()
add("noteheadWhole", w, anchors=cutouts(w))

# double whole: whole head between two pairs of bars (thick outer, thin inner)
def double_whole():
    head = move(whole(), 0.36, 0)
    L = 0.36 + WHOLE_W + 0.36
    bars = union(rect(0, -0.62, 0.14, 0.62), rect(0.22, -0.62, 0.30, 0.62),
                 rect(L - 0.14, -0.62, L, 0.62), rect(L - 0.30, -0.62, L - 0.22, 0.62))
    return union(head, bars)

add("noteheadDoubleWhole", double_whole())
add("noteheadNull", Path(), adv=HEAD_W)

# ---- X noteheads (drums): two bars with flat horizontal-cut ends
def xhead(W=1.16, H=1.0, t=0.17):
    a = bar((0, -H / 2), (W, H / 2), t)
    c = bar((0, H / 2), (W, -H / 2), t)
    p = union(a, c)
    p = inter(p, rect(0, -H / 2, W, H / 2))
    return p

xb = xhead()
add("noteheadXBlack", xb, anchors={"stemUpSE": (1.16, 0.5), "stemDownNW": (0, -0.5)})
xh = union(xhead(1.16, 1.0, 0.12))
add("noteheadXHalf", xh, anchors={"stemUpSE": (1.16, 0.5), "stemDownNW": (0, -0.5)})
xw = xhead(1.5, 1.0, 0.14)
add("noteheadXWhole", xw)

def circlex():
    D = 1.12
    r = ring(D / 2, 0, D / 2, 0.56, 0.16, 0.08)
    s = 0.56 * 0.7071
    x = union(bar((D / 2 - s, -s), (D / 2 + s, s), 0.1), bar((D / 2 - s, s), (D / 2 + s, -s), 0.1))
    x = inter(x, ellipse(D / 2, 0, D / 2 - 0.02, 0.54))
    return union(r, x)

cx_ = circlex()
add("noteheadCircleX", cx_, anchors={"stemUpSE": (1.12, 0.0), "stemDownNW": (0, 0.0)})

# ---- diamonds (harmonics)
def diamond(W, H, counter=False):
    p = poly([(0, 0), (W / 2, H / 2), (W, 0), (W / 2, -H / 2)])
    if counter:
        # thick left/right walls, thin top/bottom
        q = poly([(0.30, 0), (W / 2, H / 2 - 0.14), (W - 0.30, 0), (W / 2, -H / 2 + 0.14)])
        p = diff(p, q)
    return p

db = diamond(1.2, 1.0)
add("noteheadDiamondBlack", db, anchors={"stemUpSE": (1.2, 0.0), "stemDownNW": (0, 0.0)})
dh = diamond(1.2, 1.0, True)
add("noteheadDiamondHalf", dh, anchors={"stemUpSE": (1.2, 0.0), "stemDownNW": (0, 0.0)})
add("noteheadDiamondWhole", diamond(1.5, 1.0, True))

# ---- slash noteheads (chord slashes): slanted blocks with horizontal ends
def slash_black(W=1.9, H=2.0, top=0.72):
    return poly([(0, -H / 2), (top, -H / 2), (W, H / 2), (W - top, H / 2)])

def slash_white(W, H=2.0, top=1.0, t_side=0.26, t_top=0.12):
    outer = poly([(0, -H / 2), (top, -H / 2), (W, H / 2), (W - top, H / 2)])
    # inner parallelogram: same slope
    slope = (W - top) / H
    yi0, yi1 = -H / 2 + t_top, H / 2 - t_top
    xl0 = 0 + slope * t_top + t_side
    xr0 = top + slope * t_top - t_side
    inner = poly([(xl0, yi0), (xr0, yi0), (xr0 + slope * (yi1 - yi0), yi1), (xl0 + slope * (yi1 - yi0), yi1)])
    return diff(outer, inner)

sb = slash_black()
add("noteheadSlashHorizontalEnds", sb, anchors={"stemUpSE": (1.9, 1.0), "stemDownNW": (0, -1.0)})
sh = slash_white(2.5, top=1.1)
add("noteheadSlashWhiteHalf", sh, anchors={"stemUpSE": (2.5, 1.0), "stemDownNW": (0, -1.0)})
sw = slash_white(3.1, top=1.5, t_side=0.3)
add("noteheadSlashWhiteWhole", sw)

# ---- parentheses around noteheads
def paren_left(H=1.6, t=0.16):
    outer = ellipse(0.55, 0, 0.55, H / 2)
    inner = ellipse(0.55 + t * 0.5, 0, 0.55 - t * 0.2, H / 2 - 0.03)
    p = diff(outer, inner)
    p = inter(p, rect(0, -H, 0.4, H))
    x0, y0, x1, y1 = bounds(p)
    return move(p, -x0, 0)

pl = paren_left()
add("noteheadParenthesisLeft", pl)
add("noteheadParenthesisRight", mirror_x(pl, bounds(pl)[2] / 2))
