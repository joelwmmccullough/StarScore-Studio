"""Drawing toolkit for the StarScore Deco SMuFL font.

All coordinates are in staff spaces (sp). 1 sp = 250 font units (UPM 1000).

Construction rules (the "Modernoir pen"):
  * One pen for every stroked glyph: width depends on stroke direction.
    Vertical strokes are thick, horizontal strokes are thin (vertical stress,
    like TT Modernoir). w = wmin + (wmax - wmin) * |sin(direction)|**P.
  * Ends are cut flat: perpendicular to the stroke, or horizontal/vertical.
  * Curves are drawn as circle/ellipse-like arcs; curves often run into
    straight diagonals (Modernoir's S, 2, 3, 7).
  * Dots are squares.
"""
import math
import pathops
from pathops import Path, PathOp

UNIT = 250.0
K = 0.5523  # circle kappa


# ---------------------------------------------------------------- paths
def _new():
    return Path()


def poly(pts):
    a = 0.0
    for i in range(len(pts)):
        x0, y0 = pts[i]; x1, y1 = pts[(i + 1) % len(pts)]
        a += x0 * y1 - x1 * y0
    if a < 0:
        pts = pts[::-1]
    p = Path()
    pen = p.getPen()
    pen.moveTo(pts[0])
    for q in pts[1:]:
        pen.lineTo(q)
    pen.closePath()
    return p


def rect(x0, y0, x1, y1):
    return poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def square_dot(cx, cy, s):
    h = s / 2
    return rect(cx - h, cy - h, cx + h, cy + h)


def ellipse(cx, cy, rx, ry, rot=0.0, k=K, reverse=False):
    """Ellipse (k=K) or squircle (k>K). rot in degrees."""
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))

    def T(x, y):
        return (cx + x * c - y * s, cy + x * s + y * c)

    pts = [
        (rx, 0), (rx, k * ry), (k * rx, ry), (0, ry),
        (-k * rx, ry), (-rx, k * ry), (-rx, 0),
        (-rx, -k * ry), (-k * rx, -ry), (0, -ry),
        (k * rx, -ry), (rx, -k * ry), (rx, 0),
    ]
    pts = [T(*q) for q in pts]
    p = Path()
    pen = p.getPen()
    if reverse:
        pts = pts[::-1]
    pen.moveTo(pts[0])
    for i in range(1, 13, 3):
        pen.curveTo(pts[i], pts[i + 1], pts[i + 2])
    pen.closePath()
    return p


def stadium(cx, cy, length, width, rot=0.0):
    """Rounded slot: straight sides, semicircular ends. length along rot."""
    r = width / 2
    half = max(length / 2 - r, 0)
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    body = transform(rect(-half, -r, half, r), c, s, -s, c, cx, cy)
    e1 = ellipse(cx + half * c, cy + half * s, r, r)
    e2 = ellipse(cx - half * c, cy - half * s, r, r)
    return union(body, e1, e2)


def union(*ps):
    ps = [p for p in ps if p is not None]
    if not ps:
        return Path()
    out = pathops.simplify(ps[0], fix_winding=True)
    for p in ps[1:]:
        out = pathops.op(out, p, PathOp.UNION, fix_winding=True)
    return out


def diff(a, *bs):
    out = a
    for b in bs:
        out = pathops.op(out, b, PathOp.DIFFERENCE, fix_winding=True)
    return out


def inter(a, b):
    return pathops.op(a, b, PathOp.INTERSECTION, fix_winding=True)


def xor(a, b):
    return pathops.op(a, b, PathOp.XOR, fix_winding=True)


class _TPen:
    def __init__(self, out, m):
        self.pen = out.getPen()
        self.m = m

    def _t(self, p):
        a, b, c, d, e, f = self.m
        x, y = p
        return (a * x + c * y + e, b * x + d * y + f)

    def moveTo(self, p):
        self.pen.moveTo(self._t(p))

    def lineTo(self, p):
        self.pen.lineTo(self._t(p))

    def curveTo(self, *ps):
        self.pen.curveTo(*[self._t(p) for p in ps])

    def qCurveTo(self, *ps):
        self.pen.qCurveTo(*[self._t(p) for p in ps])

    def closePath(self):
        self.pen.closePath()

    def endPath(self):
        self.pen.endPath()


def transform(p, a, b, c, d, e=0.0, f=0.0):
    """Affine: x' = a x + c y + e ; y' = b x + d y + f"""
    out = Path()
    p.draw(_TPen(out, (a, b, c, d, e, f)))
    if a * d - b * c < 0:
        out = union(out)  # fix winding after mirror
    return out


def move(p, dx, dy):
    return transform(p, 1, 0, 0, 1, dx, dy)


def scale(p, sx, sy=None, ox=0.0, oy=0.0):
    sy = sx if sy is None else sy
    return transform(p, sx, 0, 0, sy, ox - sx * ox, oy - sy * oy)


def rotate(p, deg, ox=0.0, oy=0.0):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return transform(p, c, s, -s, c, ox - c * ox + s * oy, oy - s * ox - c * oy)


def mirror_x(p, axis=0.0):
    return transform(p, -1, 0, 0, 1, 2 * axis, 0)


def mirror_y(p, axis=0.0):
    return transform(p, 1, 0, 0, -1, 0, 2 * axis)


def slant(p, deg, oy=0.0):
    t = math.tan(math.radians(deg))
    return transform(p, 1, 0, t, 1, -t * oy, 0)


def bounds(p):
    if not list(p.segments):
        return (0, 0, 0, 0)
    return p.bounds


def halfplane(p0, p1, big=50):
    """Polygon covering the left side of the directed line p0->p1."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux
    a = (p0[0] - ux * big, p0[1] - uy * big)
    b = (p0[0] + ux * big, p0[1] + uy * big)
    return poly([a, b, (b[0] + nx * big, b[1] + ny * big), (a[0] + nx * big, a[1] + ny * big)])


LAST_CAPS = None

# ---------------------------------------------------------------- the pen
class Nib:
    def __init__(self, wmin, wmax, power=1.6, angle=0.0):
        self.wmin, self.wmax, self.power, self.angle = wmin, wmax, power, angle

    def width(self, dx, dy):
        a = math.atan2(dy, dx) - math.radians(self.angle)
        return self.wmin + (self.wmax - self.wmin) * abs(math.sin(a)) ** self.power


def _bez(p0, p1, p2, p3, t):
    mt = 1 - t
    x = mt**3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t**3 * p3[0]
    y = mt**3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t**3 * p3[1]
    return x, y


def _bezd(p0, p1, p2, p3, t):
    mt = 1 - t
    x = 3 * mt * mt * (p1[0] - p0[0]) + 6 * mt * t * (p2[0] - p1[0]) + 3 * t * t * (p3[0] - p2[0])
    y = 3 * mt * mt * (p1[1] - p0[1]) + 6 * mt * t * (p2[1] - p1[1]) + 3 * t * t * (p3[1] - p2[1])
    return x, y


def parse_skel(skel):
    """skel: list of points/commands.
    Format: [p0, ('L', p1), ('C', c1, c2, p3), ...]  -> list of cubic segments."""
    segs = []
    cur = skel[0]
    for item in skel[1:]:
        if item[0] == 'L':
            p1 = item[1]
            c1 = (cur[0] + (p1[0] - cur[0]) / 3, cur[1] + (p1[1] - cur[1]) / 3)
            c2 = (cur[0] + 2 * (p1[0] - cur[0]) / 3, cur[1] + 2 * (p1[1] - cur[1]) / 3)
            segs.append((cur, c1, c2, p1, 'L'))
            cur = p1
        elif item[0] == 'C':
            segs.append((cur, item[1], item[2], item[3], 'C'))
            cur = item[3]
        elif item[0] == 'A':  # smooth arc through a point: ('A', through, end)
            raise NotImplementedError
    return segs


def sample(segs, step=0.012):
    pts = []  # (x, y, dx, dy)
    for (p0, c1, c2, p3, kind) in segs:
        L = math.hypot(p3[0] - p0[0], p3[1] - p0[1]) + math.hypot(c1[0] - p0[0], c1[1] - p0[1]) \
            + math.hypot(c2[0] - c1[0], c2[1] - c1[1]) + math.hypot(p3[0] - c2[0], p3[1] - c2[1])
        n = max(int(L / 2 / step), 2) if kind == 'C' else max(int(L / 2 / step), 1)
        for i in range(n + 1):
            t = i / n
            if pts and i == 0:
                # smooth joint inside a run: average the incoming and outgoing directions
                d = _bezd(p0, c1, c2, p3, 1e-4)
                L1 = math.hypot(*d) or 1
                px, py, pdx, pdy, _ = pts[-1]
                L0 = math.hypot(pdx, pdy) or 1
                pts[-1] = (px, py, pdx / L0 + d[0] / L1, pdy / L0 + d[1] / L1, False)
                continue
            x, y = _bez(p0, c1, c2, p3, t)
            tt = min(max(t, 1e-4), 1 - 1e-4)
            d = _bezd(p0, c1, c2, p3, tt)
            pts.append((x, y, d[0], d[1], False))
    return pts


def stroke(skel, nib, cap0='perp', cap1='perp', wfun=None, step=0.012, ball0=None, ball1=None):
    """Stroke a skeleton with the direction-dependent nib.

    cap: 'perp' (flat, perpendicular to the stroke), 'h' (horizontal cut),
         'v' (vertical cut), or an angle in degrees for the cut line.
    wfun(s) optional multiplier by normalised position s in [0,1].
    ball0 / ball1 = (diameter, ramp_length): the stroke swells smoothly to the full
    diameter over ramp_length and ends in a round ball centred on the end point, so the
    ball grows out of the line instead of sitting on it.
    Sharp corners in the skeleton get bevel-filled joins.
    """
    segs = parse_skel(skel)
    # split into smooth runs at corners
    runs = [[segs[0]]]
    for a, b in zip(segs, segs[1:]):
        da = (a[3][0] - a[2][0], a[3][1] - a[2][1])
        if math.hypot(*da) < 1e-9:
            da = (a[3][0] - a[0][0], a[3][1] - a[0][1])
        db = (b[1][0] - b[0][0], b[1][1] - b[0][1])
        if math.hypot(*db) < 1e-9:
            db = (b[3][0] - b[0][0], b[3][1] - b[0][1])
        ang = abs(math.degrees(math.atan2(da[0] * db[1] - da[1] * db[0], da[0] * db[0] + da[1] * db[1])))
        if ang > 8:
            runs.append([b])
        else:
            runs[-1].append(b)
    total = sum(len(r) for r in runs)
    pieces = []
    ends = []
    idx = 0
    allpts = []
    for r in runs:
        pts = sample(r, step)
        allpts.append(pts)
    # cumulative length for wfun
    lens = []
    acc = 0
    flat = []
    for pts in allpts:
        seg = []
        for i, q in enumerate(pts):
            if i:
                acc += math.hypot(q[0] - pts[i - 1][0], q[1] - pts[i - 1][1])
            seg.append(acc)
        lens.append(seg)
    total_len = acc or 1
    for ri, pts in enumerate(allpts):
        left, right = [], []
        for i, (x, y, dx, dy, _) in enumerate(pts):
            L = math.hypot(dx, dy) or 1
            ux, uy = dx / L, dy / L
            w = nib.width(ux, uy)
            if wfun:
                w *= wfun(lens[ri][i] / total_len)
            for ball, dist in ((ball0, lens[ri][i]), (ball1, total_len - lens[ri][i])):
                if ball and dist < ball[1]:
                    u = 1 - dist / ball[1]
                    e = u * u * u * (u * (u * 6 - 15) + 10)      # smootherstep: flat at both ends
                    w = w + (ball[0] * 0.98 - w) * e
            nx, ny = -uy, ux
            left.append((x + nx * w / 2, y + ny * w / 2))
            right.append((x - nx * w / 2, y - ny * w / 2))
        # sweep: one small quad per sample step, merged together. Robust where the
        # inner edge folds over itself (tight curves, swelling ball terminals).
        b = pathops.OpBuilder(fix_winding=False, keep_starting_points=False)
        for i in range(len(left) - 1):
            b.add(poly([left[i], left[i + 1], right[i + 1], right[i]]), PathOp.UNION)
        if ri == 0 and ball0:
            x, y = pts[0][0], pts[0][1]
            b.add(ellipse(x, y, ball0[0] / 2, ball0[0] / 2), PathOp.UNION)
        if ri == len(allpts) - 1 and ball1:
            x, y = pts[-1][0], pts[-1][1]
            b.add(ellipse(x, y, ball1[0] / 2, ball1[0] / 2), PathOp.UNION)
        pieces.append(b.resolve())
        ends.append((left[0], right[0], left[-1], right[-1]))
    # bevel joins between runs
    for i in range(len(ends) - 1):
        a = ends[i]
        b = ends[i + 1]
        pieces.append(_hull([a[2], a[3], b[0], b[1]]))
    out = union(*pieces)
    # caps
    first = allpts[0][0]
    last = allpts[-1][-1]
    out = _cap(out, first, cap0, start=True, nib=nib)
    out = _cap(out, last, cap1, start=False, nib=nib)
    global LAST_CAPS
    LAST_CAPS = (ends[0][0], ends[0][1], ends[-1][2], ends[-1][3])
    return out


def _cap(path, q, cap, start, nib):
    """Cut the stroke end along a line through its end point. The cut only acts in a
    small box around the end, so strokes that come back past the end are untouched."""
    if cap == 'perp' or cap is None:
        return path
    x, y, dx, dy, _ = q
    if cap == 'h':
        ang = 0.0
    elif cap == 'v':
        ang = 90.0
    else:
        ang = float(cap)
    c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    p0 = (x - c, y - s)
    p1 = (x + c, y + s)
    L = math.hypot(dx, dy) or 1
    ux, uy = dx / L, dy / L
    into = (ux, uy) if start else (-ux, -uy)
    # halfplane() keeps the left side of p0->p1; we want the side AWAY from the stroke
    if (-s) * into[0] + c * into[1] > 0:
        p0, p1 = p1, p0
    w = nib.width(ux, uy) * 0.5 + 0.004           # half the stroke width at the end
    a = w * 3.0                                    # reach along the stroke, both ways
    nx, ny = -uy, ux
    box = poly([(x + ux * a + nx * w, y + uy * a + ny * w), (x - ux * a + nx * w, y - uy * a + ny * w),
                (x - ux * a - nx * w, y - uy * a - ny * w), (x + ux * a - nx * w, y + uy * a - ny * w)])
    away = inter(halfplane(p0, p1), box)
    return diff(path, away)


def _hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3:
        return None

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    h = lo[:-1] + up[:-1]
    if len(h) < 3:
        return None
    return poly(h)


def bar(p0, p1, w):
    """Straight bar of constant width w, perpendicular flat ends."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy)
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    return poly([(p0[0] + nx, p0[1] + ny), (p1[0] + nx, p1[1] + ny), (p1[0] - nx, p1[1] - ny), (p0[0] - nx, p0[1] - ny)])


def parallelogram(x0, x1, yc, h, rise):
    """Slanted thick bar (sharp/natural crossbar): from x0 to x1, centre height yc at the
    middle, vertical thickness h, rising `rise` over its width. Vertical ends."""
    xm = (x0 + x1) / 2
    k = rise / (x1 - x0)
    y0 = yc + k * (x0 - xm)
    y1 = yc + k * (x1 - xm)
    return poly([(x0, y0 - h / 2), (x1, y1 - h / 2), (x1, y1 + h / 2), (x0, y0 + h / 2)])


def arc(cx, cy, rx, ry, a0, a1):
    """Skeleton items for an elliptical arc from angle a0 to a1 (degrees).
    Returns (start_point, [('C', c1, c2, p), ...])."""
    n = max(1, int(math.ceil(abs(a1 - a0) / 90.0 - 1e-9)))
    items = []
    d = (a1 - a0) / n
    kk = 4.0 / 3.0 * math.tan(math.radians(abs(d)) / 4.0)
    sgn = 1 if d > 0 else -1
    def P(a):
        return (cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))
    def D(a):  # derivative wrt angle (per radian), direction of travel
        return (-rx * math.sin(math.radians(a)) * sgn, ry * math.cos(math.radians(a)) * sgn)
    for i in range(n):
        s, e = a0 + i * d, a0 + (i + 1) * d
        p0, p3 = P(s), P(e)
        d0, d3 = D(s), D(e)
        c1 = (p0[0] + kk * d0[0], p0[1] + kk * d0[1])
        c2 = (p3[0] - kk * d3[0], p3[1] - kk * d3[1])
        items.append(('C', c1, c2, p3))
    return P(a0), items


def caps():
    """(start_left, start_right, end_left, end_right) of the last stroke()."""
    return LAST_CAPS


def join_point(caps_a_end, caps_b_start, tip):
    """Fill the gap where two strokes meet at a sharp apex."""
    return _hull([caps_a_end[0], caps_a_end[1], caps_b_start[0], caps_b_start[1], tip])


def _lineint(p, d, q, e):
    """Intersection of p + t d and q + s e."""
    den = d[0] * e[1] - d[1] * e[0]
    if abs(den) < 1e-9:
        return None
    t = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / den
    return (p[0] + t * d[0], p[1] + t * d[1])


def polyline(pts, widths, miter_limit=3.0):
    """Straight segments of individual widths joined with miters (bolt/zigzag shapes).
    Ends are perpendicular flat cuts."""
    n = len(pts) - 1
    if isinstance(widths, (int, float)):
        widths = [widths] * n
    L, R = [], []
    segs = []
    for i in range(n):
        p, q = pts[i], pts[i + 1]
        dx, dy = q[0] - p[0], q[1] - p[1]
        l = math.hypot(dx, dy)
        ux, uy = dx / l, dy / l
        nx, ny = -uy, ux
        w = widths[i] / 2
        segs.append(((p[0] + nx * w, p[1] + ny * w), (ux, uy), (p[0] - nx * w, p[1] - ny * w)))
    L.append(segs[0][0]); R.append(segs[0][2])
    for i in range(n - 1):
        a, b = segs[i], segs[i + 1]
        joint = pts[i + 1]
        lim = miter_limit * max(widths[i], widths[i + 1]) / 2
        wa, wb = widths[i] / 2, widths[i + 1] / 2
        # end of segment a / start of segment b, on each side
        na = (-a[1][1], a[1][0]); nb = (-b[1][1], b[1][0])
        a_l = (joint[0] + na[0] * wa, joint[1] + na[1] * wa); a_r = (joint[0] - na[0] * wa, joint[1] - na[1] * wa)
        b_l = (joint[0] + nb[0] * wb, joint[1] + nb[1] * wb); b_r = (joint[0] - nb[0] * wb, joint[1] - nb[1] * wb)
        for side, (pa, pb, la, lb) in (('L', (a_l, b_l, a[0], b[0])), ('R', (a_r, b_r, a[2], b[2]))):
            m = _lineint(la, a[1], lb, b[1])
            out = L if side == 'L' else R
            if m is None or math.hypot(m[0] - joint[0], m[1] - joint[1]) > lim:
                out.append(pa); out.append(pb)          # bevel
            else:
                out.append(m)
    p, q = pts[-2], pts[-1]
    last = segs[-1]
    dx, dy = q[0] - p[0], q[1] - p[1]
    l = math.hypot(dx, dy)
    ux, uy = dx / l, dy / l
    nx, ny = -uy, ux
    w = widths[-1] / 2
    L.append((q[0] + nx * w, q[1] + ny * w)); R.append((q[0] - nx * w, q[1] - ny * w))
    return union(poly(L + R[::-1]))


def fill_holes(path, amin):
    """Drop hole contours (opposite direction to the outer ones) with area < amin."""
    cs = list(path.contours)
    if not cs:
        return path
    outer_dir = max(cs, key=lambda c: c.area).clockwise
    out = Path()
    pen = out.getPen()
    for c in cs:
        if c.clockwise != outer_dir and c.area < amin:
            continue
        c.draw(pen)
    return union(out)


def holes(path):
    cs = list(path.contours)
    if not cs:
        return []
    outer_dir = max(cs, key=lambda c: c.area).clockwise
    return [(round(c.area, 3), [round(v, 2) for v in c.bounds]) for c in cs if c.clockwise != outer_dir]


def shape(skel):
    """Closed filled shape from skeleton notation: [p0, ('L', p), ('C', c1, c2, p), ...]."""
    p = Path()
    pen = p.getPen()
    pen.moveTo(skel[0])
    for it in skel[1:]:
        if it[0] == 'L':
            pen.lineTo(it[1])
        else:
            pen.curveTo(it[1], it[2], it[3])
    pen.closePath()
    return union(p)


def poly_hull(pts):
    return _hull(pts)
