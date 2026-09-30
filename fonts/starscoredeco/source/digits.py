"""Condensed deco figures. Construction: vertical-stress rings (compass bowls)
cut flat, straight parallelogram diagonals with horizontal ends, flat bars.
Centred on y=0, left ink at x=0."""
from kit import *
from shapes import *
import math

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
    fw = 0.34 * W                  # how far the flag reaches left
    fd = 0.30 * H                  # its vertical thickness
    fl = poly([(xv0 + T * 0.5, h), (xv0, h), (xv0 - fw, h - 0.62 * fw - 0.02 * H),
               (xv0 - fw, h - 0.62 * fw - 0.02 * H - fd), (xv0 + T * 0.5, h - fd)])
    F['1'] = union(v, fl)

    # 2
    ry = 0.30 * H
    cy = h - ry
    b = bowl(xm, cy, xm, ry, T, t)
    b = diff(b, rect(-1, -5, xm, cy - 0.06 * H))            # left terminal cut flat
    b = diff(b, rect(xm, -5, 5, cy))                         # right wall ends at the centre line
    b = diff(b, rect(-5, -5, 5, cy - 0.06 * H))
    d = diag(W - T, W, cy + 1e-4, 0.0, D, -h + bt)
    base = rect(0.0, -h, W, -h + bt)
    F['2'] = inter(union(b, d, base), rect(0, -h, W, h))

    # 3 (Modernoir: flat top bar, a diagonal whose flat tip rests on the lower bowl)
    top = rect(0.04 * W, h - bt, W, h)
    ry = 0.33 * H
    cy = -h + ry
    rx = xm
    b = bowl(xm, cy, rx, ry, T, t)
    xt0 = W - D
    xb_ref = xm - D * 0.55                       # where the left edge would be at the bowl's top
    y_ref = cy + ry - t
    k = (xt0 - xb_ref) / ((h - bt) - y_ref)
    L = lambda y: xt0 - k * ((h - bt) - y)       # left edge x at height y
    R = lambda y: L(y) + D                        # right edge x
    # yB: where the right edge meets the inner ellipse (upper-left quarter)
    irx, iry = rx - T, ry - t
    def inside_inner(x, y):
        return ((x - xm) / irx) ** 2 + ((y - cy) / iry) ** 2 < 1
    lo, hi = cy, cy + iry
    for _ in range(60):
        mid = (lo + hi) / 2
        if inside_inner(R(mid), mid):
            lo = mid
        else:
            hi = mid
    yB = hi
    d = diag(xt0, W, h - bt, L(yB), R(yB), yB)
    right_line = halfplane((R(yB), yB), (W, h - bt))            # left of the diagonal's right edge
    y_term = cy - 0.08 * H
    b = diff(b, inter(right_line, rect(-5, y_term, 5, h)))
    F['3'] = inter(union(top, b, d), rect(0, -h, W, h))

    # 4 (closed triangle)
    vx1 = W - 0.06 * W
    v = rect(vx1 - T, -h, vx1, h)
    cby = -h + 0.22 * H
    d = diag(vx1 - T, vx1, h, 0.0, D * 0.82, cby)
    cb = rect(0.0, cby - 0.02, W + 0.02, cby + bt * 1.05)
    F['4'] = inter(union(v, d, cb), rect(0, -h, W + 0.04, h))

    # 5
    xl = 0.08 * W
    top = rect(xl, h - bt, W - 0.02, h)
    ry = 0.33 * H
    cy = -h + ry
    vv = rect(xl, cy + ry - t, xl + T * 0.82, h)
    b = bowl(xm, cy, xm, ry, T, t)
    b = diff(b, rect(-5, cy - 0.08 * H, xm, 5))          # keep the right half and the lower-left terminal
    flat = rect(xl, cy + ry - t, xm + 0.001, cy + ry)     # flat top of the bowl joins the vertical
    F['5'] = inter(union(top, vv, b, flat), rect(0, -h, W, h))

    # 6: lower bowl + a straight diagonal tangent to the bowl's outer curve
    ry = 0.34 * H
    cy = -h + ry
    rx = xm
    px, py = W - D - 0.05 * W, h                   # top-left corner of the diagonal
    # tangent from P to the outer ellipse (work in a space where the ellipse is a circle)
    qx, qy = px - xm, (py - cy) * rx / ry
    dq = math.hypot(qx, qy)
    base = math.atan2(qy, qx)
    off = math.acos(rx / dq)
    cands = [base + off, base - off]
    ang = min(cands, key=lambda a: math.cos(a))    # the tangent point on the left side
    tx_, ty_ = xm + rx * math.cos(ang), cy + ry * math.sin(ang)
    kx = (px - tx_) / (py - ty_)
    ylow = cy - 0.3 * ry
    xl_low = tx_ - kx * (ty_ - ylow)
    band = poly([(xl_low, ylow), (xl_low + D, ylow), (px + D, py), (px, py)])
    outer = ellipse(xm, cy, rx, ry, 0, KQ)
    inner = ellipse(xm, cy, rx - T, ry - t, 0, KQ)
    right_of = halfplane((px + D, py), (xl_low + D, ylow))
    counter = inter(inner, right_of)
    body = union(outer, inter(band, union(outer, rect(-5, ty_, 5, 5))))
    F['6'] = inter(diff(body, counter), rect(0, -h, W, h))

    # 7
    top = rect(0.0, h - bt, W, h)
    d = diag(W - D, W, h - bt, 0.18 * W, 0.18 * W + D, -h)
    F['7'] = inter(union(top, d), rect(0, -h, W, h))

    # 8
    ru = 0.235 * H
    rl = 0.285 * H
    up = bowl(xm, h - ru, xm - 0.06 * W, ru, T * 0.9, t)
    lo = bowl(xm, -h + rl, xm, rl, T, t)
    F['8'] = union(up, lo)

    F['9'] = rotate(F['6'], 180, xm, 0)
    for k in '237':
        F[k] = fill_holes(F[k], 0.02 * H * W)
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
