"""Modernoir figures. Construction: true-ellipse rings with small oval counters, straight
diagonals tangent to the bowls (2, 6, 9), diagonals ending in sharp points cut along the
horizontal (1, 4, 7), flat bars. Each figure has its own Modernoir width.
Centred on y=0, left ink at x=0."""
from kit import *
from shapes import *
import math
import style
from style import K, FIG_W


def bowl(cx, cy, rx, ry, T, t):
    return diff(ellipse(cx, cy, rx, ry, 0, K), ellipse(cx, cy, rx - T, ry - t, 0, K))


def diag(xt0, xt1, yt, xb0, xb1, yb):
    """Diagonal band with horizontal ends: top from xt0..xt1 at yt, bottom xb0..xb1 at yb."""
    return poly([(xb0, yb), (xb1, yb), (xt1, yt), (xt0, yt)])


def tangent_point(px, py, cx, cy, rx, ry, side):
    """Tangent point from P to the ellipse; side=+1 picks the tangent on P's right when
    looking from P to the centre, -1 the left."""
    qx, qy = (px - cx) / rx, (py - cy) / ry           # circle space
    d = math.hypot(qx, qy)
    base = math.atan2(qy, qx)
    off = math.acos(min(1.0, 1.0 / d))
    a = base + side * off
    return cx + rx * math.cos(a), cy + ry * math.sin(a)


def tangent_band(px, py, D, cx, cy, rx, ry, side, extend=0.4, edge='left'):
    """Straight band of horizontal width D running from the point (px,py) tangentially onto
    the ellipse, extended `extend` past the tangent point. `edge` says whether (px,py) and the
    tangent are the band's left or right edge."""
    tx, ty = tangent_point(px, py, cx, cy, rx, ry, side)
    dx, dy = tx - px, ty - py
    L = math.hypot(dx, dy)
    ex, ey = tx + dx / L * extend, ty + dy / L * extend
    o = D if edge == 'left' else -D
    return poly([(px, py), (px + o, py), (ex + o, ey), (ex, ey)]), (tx, ty)


def _figs(H, T, t, W_of):
    h = H / 2
    D = T * 1.12            # horizontal width of a diagonal stroke
    bt = t                  # bar thickness
    F = {}

    # 0
    W = W_of('0')
    F['0'] = bowl(W / 2, 0, W / 2, h, T, t)

    # 1: stem at the right, long flag down to the left, pointed
    W = W_of('1')
    v = rect(W - T, -h, W, h)
    fl = poly([(W - T * 0.15, h), (0.0, h - 0.62 * W - 0.10 * H), (0.0, h - 0.62 * W - 0.10 * H - t * 1.3),
               (W - T * 0.15, h - t * 1.3)])
    F['1'] = union(v, fl)

    # 2: round top bowl whose right wall flows into the diagonal, base bar
    W = W_of('2')
    r = W / 2
    cy = h - r
    outer = ellipse(r, cy, r, r, 0, K)
    inner = ellipse(r, cy, r - T, r - t, 0, K)
    base = rect(0.0, -h, W, -h + bt)
    band, (tx, ty) = tangent_band(D, -h + bt * 0.999, D, r, cy, r, r, +1, edge='right')
    kept = diff(outer, rect(-5, -5, r, cy - 0.11 * H), rect(r, -5, 5, ty + 0.06 * H))
    F['2'] = inter(diff(union(kept, band, base), diff(inner, band)), rect(0, -h, W, h))

    # 3: flat top bar, diagonal down to the bowl's top, round lower bowl open at the upper left
    W = W_of('3')
    Tb = T * 0.84
    ry = 0.31 * H
    cy = -h + ry
    rx = W / 2
    b = bowl(rx, cy, rx, ry, Tb, t)
    b = diff(b, rect(-5, cy - 0.05 * H, rx, 5))
    xb0 = rx - D * 0.55
    d = diag(W - D, W, h - bt, xb0, xb0 + D, cy + ry - t)
    top = rect(0.03, h - bt, W, h)
    F['3'] = inter(union(top, b, d), rect(0, -h, W, h))

    # 4: closed pointed top
    W = W_of('4')
    vx1 = W - 0.06 * W
    v = rect(vx1 - T, -h, vx1, h)
    cby = -h + 0.24 * H
    d = diag(vx1 - T, vx1, h, 0.0, D * 0.85, cby)
    cb = rect(0.0, cby, W + 0.02, cby + bt)
    F['4'] = inter(union(v, d, cb), rect(0, -h, W + 0.04, h))

    # 5: bar, short vertical, round bowl open at the upper left
    W = W_of('5')
    Tb = T * 0.84
    ry = 0.31 * H
    cy = -h + ry
    rx = W / 2
    xl = 0.04 * W
    top = rect(xl, h - bt, W, h)
    vv = rect(xl, cy + ry - t, xl + T * 0.9, h)
    b = bowl(rx, cy, rx, ry, Tb, t)
    b = diff(b, rect(-5, cy - 0.05 * H, rx, 5))
    flat = rect(xl, cy + ry - t, rx + 0.001, cy + ry)
    F['5'] = inter(union(top, vv, b, flat), rect(0, -h, W, h))

    # 6: round lower bowl, tangent diagonal rising to a point at the top right
    W = W_of('6')
    r = W / 2
    cy = -h + r
    px, py = W - D * 1.05, h
    band, (tx, ty) = tangent_band(px, py, D, r, cy, r, r, +1, extend=0.5)
    outer = ellipse(r, cy, r, r, 0, K)
    inner = ellipse(r, cy, r - T, r - t, 0, K)
    F['6'] = inter(diff(union(outer, band), diff(inner, band)), rect(0, -h, W, h))

    # 7
    W = W_of('7')
    top = rect(0.0, h - bt, W, h)
    d = diag(W - D, W, h - bt, 0.16 * W, 0.16 * W + D, -h)
    F['7'] = inter(union(top, d), rect(0, -h, W, h))

    # 8: two rings, the upper a little smaller
    W = W_of('8')
    ru = 0.24 * H
    rl = 0.27 * H
    up = bowl(W / 2, h - ru, W / 2 - 0.05 * W, ru, T * 0.95, t)
    lo = bowl(W / 2, -h + rl, W / 2, rl, T, t)
    F['8'] = union(up, lo)

    F['9'] = rotate(F['6'], 180, W_of('6') / 2, 0)
    for k in F:
        x0 = bounds(F[k])[0]
        F[k] = move(F[k], -x0, 0)
    return F


_cache = {}


def figs(H=2.0, T=None, t=None, W=None):
    """W: optional override of the widest figure (0); other figures scale with Modernoir ratios."""
    T = style.STEM_FIG * H / 2 if T is None else T
    t = style.thin(T) if t is None else t
    key = (H, T, t, W)
    if key not in _cache:
        scale_ = 1.0 if W is None else W / (FIG_W['0'] * H)
        _cache[key] = _figs(H, T, t, lambda ch: FIG_W[ch] * H * scale_)
    return _cache[key]
