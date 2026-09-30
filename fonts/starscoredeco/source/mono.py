"""Monoline construction (0.3): skeleton centrelines buffered to an exact, even stroke width.

Every stroke of a figure or letter has the same perpendicular thickness, curves run into
straight lines tangentially, and a whole letter is one skeleton, so there are no seams or
steps where parts meet. Shapely does the offsetting; the result is converted to a pathops
Path so it mixes with the rest of the kit."""
import math
from shapely.geometry import LineString, Polygon, MultiPolygon, box
from shapely.ops import unary_union
from shapely import affinity
from kit import Path, poly, union, diff, inter

RES = 64          # segments per quarter circle in buffers


# ------------------------------------------------------------ skeleton pieces
def arc(cx, cy, rx, ry, a0, a1, step=1.0):
    """Points on an ellipse from angle a0 to a1 (degrees, either direction)."""
    n = max(2, int(abs(a1 - a0) / step))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def ell_pt(cx, cy, rx, ry, a):
    return (cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))


def ell_angle(cx, cy, rx, ry, p):
    return math.degrees(math.atan2((p[1] - cy) / ry, (p[0] - cx) / rx))


def tangent_from(p, cx, cy, rx, ry, side):
    """Point where a line from p touches the ellipse. side=+1/-1 picks the two tangents."""
    qx, qy = (p[0] - cx) / rx, (p[1] - cy) / ry
    d = math.hypot(qx, qy)
    a = math.atan2(qy, qx) + side * math.acos(min(1.0, 1.0 / d))
    return (cx + rx * math.cos(a), cy + ry * math.sin(a))


def common_tangent(E1, E2, internal=True, which=0):
    """Tangent line touching two ellipses E=(cx,cy,rx,ry). Returns (p1, p2) on E1, E2.
    internal: the line crosses between them (an S spine or an 8 waist). which: 0 or 1."""
    def supp(E, nx, ny):
        cx, cy, rx, ry = E
        s = math.hypot(rx * nx, ry * ny)
        return cx * nx + cy * ny, s, (cx + rx * rx * nx / s, cy + ry * ry * ny / s)

    def f(th):
        nx, ny = math.cos(th), math.sin(th)
        c1, s1, _ = supp(E1, nx, ny)
        c2, s2, _ = supp(E2, nx, ny)
        return (c1 + s1) - (c2 - s2) if internal else (c1 + s1) - (c2 + s2)

    roots = []
    N = 3600
    prev = f(0)
    for i in range(1, N + 1):
        th = 2 * math.pi * i / N
        cur = f(th)
        if prev * cur < 0:
            a, b = 2 * math.pi * (i - 1) / N, th
            for _ in range(60):
                m = (a + b) / 2
                if f(a) * f(m) <= 0:
                    b = m
                else:
                    a = m
            roots.append((a + b) / 2)
        prev = cur
    th = roots[which]
    nx, ny = math.cos(th), math.sin(th)
    _, _, p1 = supp(E1, nx, ny)
    _, _, p2 = supp(E2, -nx, -ny) if internal else supp(E2, nx, ny)
    return p1, p2


def line(p, q, n=24):
    return [(p[0] + (q[0] - p[0]) * i / n, p[1] + (q[1] - p[1]) * i / n) for i in range(n + 1)]


def extend(pts, d0=0.0, d1=0.0):
    """Extend a polyline straight past its ends (along the end tangents)."""
    pts = list(pts)
    if d0:
        (x0, y0), (x1, y1) = pts[0], pts[1]
        L = math.hypot(x0 - x1, y0 - y1)
        pts.insert(0, (x0 + (x0 - x1) / L * d0, y0 + (y0 - y1) / L * d0))
    if d1:
        (x0, y0), (x1, y1) = pts[-1], pts[-2]
        L = math.hypot(x0 - x1, y0 - y1)
        pts.append((x0 + (x0 - x1) / L * d1, y0 + (y0 - y1) / L * d1))
    return pts


def chain(*parts):
    out = []
    for p in parts:
        p = list(p)
        if out and math.hypot(out[-1][0] - p[0][0], out[-1][1] - p[0][1]) < 1e-9:
            p = p[1:]
        out += p
    return out


# ------------------------------------------------------------ buffering
def buf(pts, w, cap='flat', join='mitre', mitre=4.0):
    """Stroke a centreline with an even width w. cap: flat | round | square."""
    cs = {'flat': 'flat', 'round': 'round', 'square': 'square'}[cap]
    return LineString(pts).buffer(w / 2, quad_segs=RES, cap_style=cs, join_style=join, mitre_limit=mitre)


def ring(cx, cy, rx, ry, w):
    """Closed elliptical stroke of width w on the centreline ellipse."""
    pts = arc(cx, cy, rx, ry, 0, 360, 0.5)
    outer = Polygon(pts).buffer(w / 2, quad_segs=RES, join_style='round')
    inner = Polygon(pts).buffer(-w / 2, quad_segs=RES, join_style='round')
    return outer.difference(inner)


def G(*geoms):
    return unary_union([g for g in geoms if g is not None])


def clip(g, x0, y0, x1, y1):
    return g.intersection(box(x0, y0, x1, y1))


def to_path(g):
    """Shapely (Multi)Polygon -> pathops Path (outer contours CCW, holes CW, as poly() does)."""
    g = g.buffer(0)
    polys = list(g.geoms) if isinstance(g, MultiPolygon) else [g]
    out = []
    for pg in polys:
        if pg.is_empty:
            continue
        o = poly(list(pg.exterior.coords)[:-1])
        for h in pg.interiors:
            o = diff(o, poly(list(h.coords)[:-1]))
        out.append(o)
    return union(*out) if len(out) > 1 else out[0]


def S(g, deg=0.0, dx=0.0, dy=0.0):
    """Slant (shear) about y=0 then move."""
    if deg:
        g = affinity.skew(g, xs=deg, origin=(0, 0))
    if dx or dy:
        g = affinity.translate(g, dx, dy)
    return g


def thin_points(g, tol):
    """Drop outline points closer than tol to the last kept one. Keeps curves (points stay dense
    enough for the curve fitter) but removes the micro-edges a buffer leaves at cap corners,
    which otherwise survive as tiny steps."""
    def ring(coords):
        pts = list(coords)[:-1]
        out = [pts[0]]
        for p in pts[1:]:
            if math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) >= tol:
                out.append(p)
        if len(out) > 3 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < tol:
            out.pop()
        return out
    polys = list(g.geoms) if isinstance(g, MultiPolygon) else [g]
    res = [Polygon(ring(p.exterior.coords), [ring(h.coords) for h in p.interiors]) for p in polys]
    return unary_union(res)
