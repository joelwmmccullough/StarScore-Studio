"""Modernoir figures, 0.3: monoline. Each figure is one centreline stroked at an even width,
so curves run into straight lines without seams or steps and every stroke weighs the same.
Centred on y=0, left ink at x=0."""
import math
from kit import *
from shapes import *
import style
from style import FIG_W
import mono as M


def diag(xt0, xt1, yt, xb0, xb1, yb):
    """Diagonal band with horizontal ends (kept for older callers)."""
    return poly([(xb0, yb), (xb1, yb), (xt1, yt), (xt0, yt)])


def band(p, q, w, side=+1):
    """Straight band of perpendicular width w whose one edge runs along the line p->q.
    side=+1 puts the band to the left of p->q, -1 to the right. Extended far past both ends."""
    dx, dy = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy * side, ux * side
    a = (p[0] - ux * 5, p[1] - uy * 5)
    b = (q[0] + ux * 5, q[1] + uy * 5)
    return M.Polygon([a, b, (b[0] + nx * w, b[1] + ny * w), (a[0] + nx * w, a[1] + ny * w)])


def _figs(H, w, W_of):
    h = H / 2
    e = w / 2
    F = {}
    box = lambda W: (0, -h, W, h)

    # 0 ---------------------------------------------------------------
    W = W_of('0')
    F['0'] = M.ring(W / 2, 0, W / 2 - e, h - e, w)

    # 1: stem at the right; a straight flag from the stem top down to the left, cut vertically
    W = W_of('1')
    stem = M.box(W - w, -h, W, h)
    th = math.radians(36)
    ux, uy = math.cos(th), math.sin(th)          # flag direction, rising toward the stem
    Q = (0.0, h - (W - w) * math.tan(th))        # the flag's upper edge meets the stem top corner
    nx, ny = uy, -ux                              # below the upper edge
    Q2 = (Q[0] + nx * w, Q[1] + ny * w)
    flag = M.Polygon([Q, Q2, (Q2[0] + ux * 4, Q2[1] + uy * 4), (Q[0] + ux * 4, Q[1] + uy * 4)])
    F['1'] = M.clip(M.G(stem, flag), *box(W))

    # 2: round top whose right side runs tangentially into the diagonal, then the base
    W = W_of('2')
    r = W / 2 - e
    cy = h - e - r
    P = (e * 0.9, -h + e)                     # centreline corner at the base
    T = M.tangent_from(P, W / 2, cy, r, r, -1)
    aT = M.ell_angle(W / 2, cy, r, r, T)
    if aT > 90:
        T = M.tangent_from(P, W / 2, cy, r, r, +1)
        aT = M.ell_angle(W / 2, cy, r, r, T)
    sk = M.chain(M.arc(W / 2, cy, r, r, 160, aT), M.line(T, P), M.line(P, (W, -h + e)))
    F['2'] = M.clip(M.buf(sk, w, join='mitre', mitre=8), *box(W))

    # 3: flat top, a diagonal that sits down on the bowl's top stroke, round bowl open at the
    #    upper left
    W = W_of('3')
    rx = W / 2 - e
    ry = 0.29 * H - e
    cy = -h + e + ry
    ybot = cy + ry - e                        # lower edge of the bowl's top stroke
    bowl3 = M.buf(M.arc(W / 2, cy, rx, ry, 150, -168), w)
    xl = W / 2 - 0.02 * W                     # where the diagonal's left edge lands
    xr = xl + w
    for _ in range(30):
        dx, dy = W - xr, (h - w) - ybot
        xr = xl + w * math.hypot(dx, dy) / dy
    d3 = M.clip(band((W, h - w), (xr, ybot), w, side=-1), -1, cy, W, h)
    # the diagonal's left edge (through xl at ybot) continues straight down until it meets the
    # counter: everything left of it and above the bowl's centre goes
    ux, uy = (W - xr), ((h - w) - ybot)
    L = math.hypot(ux, uy); ux, uy = ux / L, uy / L
    pL = (xl, ybot)
    left = M.Polygon([(pL[0] - ux * 5, pL[1] - uy * 5), (pL[0] + ux * 5, pL[1] + uy * 5), (-5, 5), (-5, -5)])
    left = left.intersection(M.box(-5, cy, 5, h - w))
    counter = M.Polygon(M.arc(W / 2, cy, rx - e, ry - e, 0, 360))
    g3 = M.G(M.box(0.0, h - w, W, h), d3, bowl3).difference(left).difference(counter)
    F['3'] = M.clip(g3, *box(W))

    # 4: closed pointed top, crossbar running past the stem
    W = W_of('4')
    xs1 = W / 2 + 0.050 * H + w / 2           # stem right edge: stem centre just right of centre, like the 1's
    xs0 = xs1 - w
    cb = -h + 0.25 * H                        # crossbar bottom
    outer = M.Polygon([(xs0, -h), (xs1, -h), (xs1, cb), (W, cb), (W, cb + w), (xs1, cb + w),
                       (xs1, h), (xs0, h), (0.0, cb), (xs0, cb)])
    # counter: inside the diagonal (offset w from the outer diagonal edge), above the bar
    dx, dy = xs0 - 0.0, h - cb
    L = math.hypot(dx, dy)
    nx, ny = dy / L, -dx / L                  # unit normal pointing into the figure
    q0 = (0.0 + nx * w, cb + ny * w)
    k = dy / dx
    xa = q0[0] + (cb + w - q0[1]) / k         # inner edge at the bar top
    ya = q0[1] + k * (xs0 - q0[0])            # inner edge at the stem's left side
    counter = M.Polygon([(xa, cb + w), (xs0, cb + w), (xs0, ya)])
    F['4'] = outer.difference(counter)

    # 5: bar, short vertical, flat run into a round bowl open at the lower left
    W = W_of('5')
    xl = 0.03 * W
    rx = W / 2 - e
    ry = 0.29 * H - e
    cy = -h + e + ry
    top = cy + ry
    sk = M.chain(M.line((W, h - e), (xl + e, h - e)), M.line((xl + e, h - e), (xl + e, top)),
                 M.line((xl + e, top), (W / 2, top)), M.arc(W / 2, cy, rx, ry, 90, -155))
    F['5'] = M.clip(M.buf(sk, w, join='mitre', mitre=8), *box(W))

    # 6: round bowl; a straight diagonal leaves it tangentially and rises to the top right,
    #    cut along the top line
    W = W_of('6')
    rx = W / 2 - e
    ry = 0.30 * H - e
    cy = -h + e + ry
    Ptop = (W - e - 0.02 * W, h + 0.4)
    T = M.tangent_from(Ptop, W / 2, cy, rx, ry, +1)
    if T[0] > W / 2:
        T = M.tangent_from(Ptop, W / 2, cy, rx, ry, -1)
    aT = M.ell_angle(W / 2, cy, rx, ry, T)
    sk = M.chain(M.line(Ptop, T), M.arc(W / 2, cy, rx, ry, aT, aT + 360 + 8))
    g6 = M.clip(M.buf(sk, w, join='round'), *box(W + 1))
    F['6'] = g6

    # 7: bar and a straight diagonal; the diagonal's right edge starts at the bar's lower right
    W = W_of('7')
    bar_ = M.box(0.0, h - w, W, h)
    xb = 0.16 * W
    xr = xb + w * 1.2
    for _ in range(30):                       # right edge bottom so the left edge lands at xb
        dx, dy = W - xr, 2 * h - w
        xr = xb + w * math.hypot(dx, dy) / dy
    d = band((W, h - w), (xr, -h), w, side=-1)
    F['7'] = M.clip(M.G(bar_, d), *box(W))

    # 8: smaller upper loop on a larger lower loop; they share one stroke at the waist
    W = W_of('8')
    ryl = 0.262 * H - e
    ryu = (H - w) / 2 - ryl
    rxl = W / 2 - e
    rxu = rxl * 0.92
    cyl = -h + e + ryl
    cyu = cyl + ryl + ryu
    F['8'] = M.G(M.ring(W / 2, cyu, rxu, ryu, w), M.ring(W / 2, cyl, rxl, ryl, w))

    F['9'] = M.affinity.rotate(F['6'], 180, origin=(W_of('6') / 2, 0))
    out = {}
    for k in F:
        p = M.to_path(F[k])
        out[k] = move(p, -bounds(p)[0], 0)
    return out


FIG_STROKE = 0.31      # stroke width of a 2 sp figure
_cache = {}


def figs(H=2.0, w=None, W=None):
    """w: stroke width (default Modernoir 0.18 cap). W: optional width of the 0; the others
    keep Modernoir's ratios."""
    w = FIG_STROKE * H / 2 if w is None else w
    key = (H, w, W)
    if key not in _cache:
        k = 1.0 if W is None else W / (FIG_W['0'] * H)
        _cache[key] = _figs(H, w, lambda ch: FIG_W[ch] * H * k)
    return _cache[key]
