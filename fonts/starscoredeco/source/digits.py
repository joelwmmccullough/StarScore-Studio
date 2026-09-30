"""Condensed deco figures. Construction: vertical-stress rings (compass bowls)
cut flat, straight parallelogram diagonals with horizontal ends, flat bars.
Centred on y=0, left ink at x=0."""
from kit import *
from shapes import *

KQ = 0.62  # squircle kappa for bowls


def bowl(cx, cy, rx, ry, T, t):
    return diff(ellipse(cx, cy, rx, ry, 0, KQ), ellipse(cx, cy, rx - T, ry - t, 0, KQ))


def diag(xt0, xt1, yt, xb0, xb1, yb):
    """Diagonal band with horizontal ends: top from xt0..xt1 at yt, bottom xb0..xb1 at yb."""
    return poly([(xb0, yb), (xb1, yb), (xt1, yt), (xt0, yt)])


def _figs(H, T, t, W):
    h = H / 2
    xm = W / 2
    D = T * 1.12            # horizontal width of a diagonal stroke
    bt = t * 1.25           # bar thickness
    F = {}

    F['0'] = bowl(xm, 0, xm, h, T, t)

    xv0 = xm - T / 2 + 0.08 * W
    v = rect(xv0, -h, xv0 + T, h)
    fl = poly([(xv0 + 0.01, h), (xv0 + 0.01, h - 0.44 * H / 2 - 0.1), (xv0 - 0.34 * W, h - 0.62 * H / 2 - 0.1), (xv0 - 0.34 * W, h - 0.30 * H / 2 - 0.1)])
    F['1'] = union(v, fl)

    # 2
    ry = 0.30 * H
    cy = h - ry
    b = bowl(xm, cy, xm, ry, T, t)
    b = diff(b, rect(-1, -5, xm, cy - 0.06 * H))            # left terminal cut flat
    b = diff(b, rect(-1, -5, 5, cy - 0.12 * H))              # nothing below the joint
    d = diag(W - T - 0.01, W, cy - 0.02, 0.0, D, -h + bt)
    base = rect(0.0, -h, W, -h + bt)
    F['2'] = inter(union(b, d, base), rect(0, -h, W, h))

    # 3 (Modernoir: flat top bar, diagonal, lower bowl)
    top = rect(0.04 * W, h - bt, W, h)
    ry = 0.33 * H
    cy = -h + ry
    b = bowl(xm, cy, xm, ry, T, t)
    b = diff(b, rect(-1, cy - 0.08 * H, xm - D * 0.55, 5), rect(-1, cy - 0.08 * H, xm, cy + ry - t))
    d = diag(W - D - 0.02, W, h - bt + 0.01, xm - D * 0.55, xm + D * 0.45, cy + ry - t)
    F['3'] = inter(union(top, b, d), rect(0, -h, W, h))

    # 4 (closed triangle)
    vx1 = W - 0.06 * W
    v = rect(vx1 - T, -h, vx1, h)
    cby = -h + 0.22 * H
    d = diag(vx1 - T * 0.9, vx1, h, 0.0, D * 0.82, cby)
    cb = rect(0.0, cby - 0.02, W + 0.02, cby + bt * 1.05)
    F['4'] = inter(union(v, d, cb), rect(0, -h, W + 0.04, h))

    # 5
    xl = 0.08 * W
    top = rect(xl, h - bt, W - 0.02, h)
    vv = rect(xl, 0.02 * H, xl + T * 0.82, h)
    ry = 0.33 * H
    cy = -h + ry
    b = bowl(xm, cy, xm, ry, T, t)
    b = diff(b, rect(-1, cy - 0.08 * H, xl + 0.02, 5), rect(-1, cy - 0.08 * H, xm, cy + ry - t))
    F['5'] = inter(union(top, vv, b), rect(0, -h, W, h))

    # 6: lower bowl + straight diagonal rising to the top right
    ry = 0.34 * H
    cy = -h + ry
    b = bowl(xm, cy, xm, ry, T, t)
    d = diag(W - D - 0.05 * W, W - 0.05 * W, h, 0.0, T, cy)
    # solid join: fill the bowl's outline above its centre, left of the diagonal's right edge
    edge = poly([(T, cy), (W - 0.05 * W, h), (-2, h), (-2, cy)])
    fill = inter(ellipse(xm, cy, xm, ry, 0, KQ), edge)
    F['6'] = inter(union(b, d, fill), rect(0, -h, W, h))

    # 7
    top = rect(0.0, h - bt, W, h)
    d = diag(W - D - 0.02, W, h - bt + 0.01, 0.18 * W, 0.18 * W + D, -h)
    F['7'] = inter(union(top, d), rect(0, -h, W, h))

    # 8
    ru = 0.235 * H
    rl = 0.285 * H
    up = bowl(xm, h - ru, xm - 0.06 * W, ru, T * 0.9, t)
    lo = bowl(xm, -h + rl, xm, rl, T, t)
    F['8'] = union(up, lo)

    F['9'] = rotate(F['6'], 180, xm, 0)
    for k in '237':
        F[k] = fill_holes(F[k], 0.08 * H * W)
    for k in F:
        x0 = bounds(F[k])[0]
        F[k] = move(F[k], -x0, 0)
    return F


_cache = {}


def figs(H=2.0, T=0.46, t=0.16, W=1.36):
    key = (H, T, t, W)
    if key not in _cache:
        _cache[key] = _figs(H, T, t, W)
    return _cache[key]
