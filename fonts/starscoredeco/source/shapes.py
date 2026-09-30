"""Shared shapes and measuring helpers."""
import math
from kit import *


class _Rec:
    """Records flattened outline points of a path."""
    def __init__(self, n=24):
        self.pts = []
        self.cur = None
        self.start = None
        self.n = n

    def moveTo(self, p):
        self.cur = p; self.start = p; self.pts.append(p)

    def lineTo(self, p):
        a = self.cur
        for i in range(1, 9):
            t = i / 8
            self.pts.append((a[0] + (p[0] - a[0]) * t, a[1] + (p[1] - a[1]) * t))
        self.cur = p

    def curveTo(self, c1, c2, p):
        a = self.cur
        for i in range(1, self.n + 1):
            t = i / self.n
            mt = 1 - t
            self.pts.append((mt**3 * a[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t**3 * p[0],
                             mt**3 * a[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t**3 * p[1]))
        self.cur = p

    def qCurveTo(self, *ps):
        # approximate
        for p in ps:
            self.lineTo(p)

    def closePath(self):
        if self.start:
            self.lineTo(self.start)

    def endPath(self):
        pass


def outline(path, n=24):
    r = _Rec(n)
    path.draw(r)
    return r.pts


def fit_ellipse_box(W, H, rot, k=K):
    """Return a shape (rotated ellipse/squircle) whose bbox is exactly W x H,
    placed with left edge at x=0 and centred on y=0."""
    rx, ry = W / 2, H / 2
    for _ in range(40):
        p = ellipse(0, 0, rx, ry, rot, k)
        x0, y0, x1, y1 = bounds(p)
        bw, bh = x1 - x0, y1 - y0
        rx *= (W / bw) ** 0.7
        ry *= (H / bh) ** 0.7
    p = ellipse(0, 0, rx, ry, rot, k)
    x0, y0, x1, y1 = bounds(p)
    # exact normalisation (tiny)
    p = transform(p, W / (x1 - x0), 0, 0, H / (y1 - y0), 0, 0)
    x0, y0, x1, y1 = bounds(p)
    return move(p, -x0, -(y0 + y1) / 2), rx, ry


def cutouts(path, ycut=0.3):
    """Approximate SMuFL cut-out corners for a notehead-like shape."""
    pts = outline(path)
    x0, y0, x1, y1 = bounds(path)
    top = [p for p in pts if p[1] >= ycut]
    bot = [p for p in pts if p[1] <= -ycut]
    nw = (min(p[0] for p in top), ycut) if top else (x0, y1)
    ne = (max(p[0] for p in top), ycut) if top else (x1, y1)
    sw = (min(p[0] for p in bot), -ycut) if bot else (x0, y0)
    se = (max(p[0] for p in bot), -ycut) if bot else (x1, y0)
    return {"cutOutNW": nw, "cutOutNE": ne, "cutOutSW": sw, "cutOutSE": se}


def edge_y(path, side='right'):
    pts = outline(path, 64)
    x0, y0, x1, y1 = bounds(path)
    if side == 'right':
        cand = [p for p in pts if p[0] > x1 - 0.004]
    else:
        cand = [p for p in pts if p[0] < x0 + 0.004]
    return sum(p[1] for p in cand) / len(cand)


def ring(cx, cy, rx, ry, t_side, t_top, rot=0.0, k=K):
    """Vertical-stress ring: thick sides (t_side), thin top/bottom (t_top)."""
    outer = ellipse(cx, cy, rx, ry, rot, k)
    inner = ellipse(cx, cy, rx - t_side, ry - t_top, rot, k)
    return diff(outer, inner)


import os
TERMINAL = os.environ.get("DECO_TERMINAL", "disc")


def term(cx, cy, s, style=None):
    """Terminal knob / dot of nominal size s (the square's side it replaces)."""
    style = style or TERMINAL
    if style == "square":
        return square_dot(cx, cy, s)
    if style == "disc":
        r = s * 0.58
        return ellipse(cx, cy, r, r)
    if style == "oval":   # tall oval, like Modernoir's narrow o
        return ellipse(cx, cy, s * 0.46, s * 0.66)
    if style == "diamond":
        r = s * 0.72
        return poly([(cx - r, cy), (cx, cy + r), (cx + r, cy), (cx, cy - r)])
    if style == "none":
        return None
    raise ValueError(style)
