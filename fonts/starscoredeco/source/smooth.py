"""Replace runs of short line segments with fitted cubic Beziers (Schneider 1990)."""
import math
from pathops import Path

SHORT = 0.07      # sp: lines shorter than this may be part of a sampled curve
MAXTURN = 16.0    # degrees: turning more than this between segments is a corner
TOL = 0.0035      # sp: max fitting error (~0.9 font units)


def _sub(a, b): return (a[0] - b[0], a[1] - b[1])
def _add(a, b): return (a[0] + b[0], a[1] + b[1])
def _mul(a, k): return (a[0] * k, a[1] * k)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1]
def _len(a): return math.hypot(a[0], a[1])
def _norm(a):
    l = _len(a)
    return (a[0] / l, a[1] / l) if l else (0.0, 0.0)


def _bez(c, t):
    mt = 1 - t
    return (mt**3 * c[0][0] + 3 * mt * mt * t * c[1][0] + 3 * mt * t * t * c[2][0] + t**3 * c[3][0],
            mt**3 * c[0][1] + 3 * mt * mt * t * c[1][1] + 3 * mt * t * t * c[2][1] + t**3 * c[3][1])


def _bezd(c, t):
    mt = 1 - t
    return (3 * mt * mt * (c[1][0] - c[0][0]) + 6 * mt * t * (c[2][0] - c[1][0]) + 3 * t * t * (c[3][0] - c[2][0]),
            3 * mt * mt * (c[1][1] - c[0][1]) + 6 * mt * t * (c[2][1] - c[1][1]) + 3 * t * t * (c[3][1] - c[2][1]))


def _bezdd(c, t):
    return (6 * (1 - t) * (c[2][0] - 2 * c[1][0] + c[0][0]) + 6 * t * (c[3][0] - 2 * c[2][0] + c[1][0]),
            6 * (1 - t) * (c[2][1] - 2 * c[1][1] + c[0][1]) + 6 * t * (c[3][1] - 2 * c[2][1] + c[1][1]))


def _chord_params(pts):
    u = [0.0]
    for i in range(1, len(pts)):
        u.append(u[-1] + _len(_sub(pts[i], pts[i - 1])))
    L = u[-1] or 1
    return [x / L for x in u]


def _gen(pts, u, t1, t2):
    p0, p3 = pts[0], pts[-1]
    C = [[0, 0], [0, 0]]
    X = [0, 0]
    for i, ui in enumerate(u):
        mt = 1 - ui
        a1 = _mul(t1, 3 * mt * mt * ui)
        a2 = _mul(t2, 3 * mt * ui * ui)
        C[0][0] += _dot(a1, a1); C[0][1] += _dot(a1, a2); C[1][1] += _dot(a2, a2)
        tmp = _sub(pts[i], _bez((p0, p0, p3, p3), ui))
        X[0] += _dot(a1, tmp); X[1] += _dot(a2, tmp)
    C[1][0] = C[0][1]
    det = C[0][0] * C[1][1] - C[1][0] * C[0][1]
    seg = _len(_sub(p3, p0))
    if abs(det) > 1e-12:
        al = (X[0] * C[1][1] - X[1] * C[0][1]) / det
        ar = (C[0][0] * X[1] - C[1][0] * X[0]) / det
    else:
        al = ar = seg / 3
    if al < 1e-6 * seg or ar < 1e-6 * seg or al > 2 * seg or ar > 2 * seg:
        al = ar = seg / 3
    return (p0, _add(p0, _mul(t1, al)), _add(p3, _mul(t2, ar)), p3)


def _maxerr(pts, c, u):
    worst, idx = 0.0, len(pts) // 2
    for i in range(1, len(pts) - 1):
        d = _len(_sub(_bez(c, u[i]), pts[i]))
        if d > worst:
            worst, idx = d, i
    return worst, idx


def _reparam(pts, c, u):
    out = []
    for p, t in zip(pts, u):
        d = _sub(_bez(c, t), p)
        d1 = _bezd(c, t)
        d2 = _bezdd(c, t)
        num = _dot(d, d1)
        den = _dot(d1, d1) + _dot(d, d2)
        out.append(min(1, max(0, t - num / den)) if abs(den) > 1e-12 else t)
    return out


def fit(pts, t1, t2, tol=TOL, depth=0):
    if len(pts) == 2:
        d = _len(_sub(pts[1], pts[0])) / 3
        return [(pts[0], _add(pts[0], _mul(t1, d)), _add(pts[1], _mul(t2, d)), pts[1])]
    u = _chord_params(pts)
    c = _gen(pts, u, t1, t2)
    err, idx = _maxerr(pts, c, u)
    if err < tol:
        return [c]
    if err < tol * 6:
        for _ in range(6):
            u = _reparam(pts, c, u)
            c = _gen(pts, u, t1, t2)
            err, idx = _maxerr(pts, c, u)
            if err < tol:
                return [c]
    if depth > 12:
        return [c]
    tc = _norm(_sub(pts[idx - 1], pts[idx + 1]))
    return fit(pts[:idx + 1], t1, tc, tol, depth + 1) + fit(pts[idx:], _mul(tc, -1), t2, tol, depth + 1)


def _turn(a, b):
    return abs(math.degrees(math.atan2(a[0] * b[1] - a[1] * b[0], _dot(a, b))))


def smooth_path(path):
    out = Path()
    pen = out.getPen()
    for contour in path.contours:
        segs = list(contour.segments)
        # collect as list of (kind, points) with explicit start points
        items = []
        start = None
        cur = None
        for verb, pts in segs:
            if verb == 'moveTo':
                start = cur = pts[0]
            elif verb == 'lineTo':
                items.append(('L', cur, pts[0])); cur = pts[0]
            elif verb == 'curveTo':
                items.append(('C', cur, pts)); cur = pts[-1]
            elif verb == 'qCurveTo':
                items.append(('Q', cur, pts)); cur = pts[-1]
            elif verb == 'closePath':
                if cur != start:
                    items.append(('L', cur, start))
                cur = start
        if not items:
            continue
        # group: runs of short lines with small turning angles
        groups = []
        for it in items:
            if it[0] == 'L' and _len(_sub(it[2], it[1])) < SHORT:
                if groups and groups[-1][0] == 'run':
                    prev = groups[-1][1][-1]
                    a = _sub(prev[2], prev[1]); b = _sub(it[2], it[1])
                    if _len(a) > 0 and _len(b) > 0 and _turn(a, b) < MAXTURN:
                        groups[-1][1].append(it)
                        continue
                groups.append(['run', [it]])
            else:
                groups.append(['one', it])
        pen.moveTo(items[0][1])
        for g in groups:
            if g[0] == 'one':
                it = g[1]
                if it[0] == 'L':
                    pen.lineTo(it[2])
                elif it[0] == 'C':
                    pen.curveTo(*it[2])
                else:
                    pen.qCurveTo(*it[2])
            else:
                run = g[1]
                if len(run) < 3:
                    for it in run:
                        pen.lineTo(it[2])
                    continue
                pts = [run[0][1]] + [it[2] for it in run]
                t1 = _norm(_sub(pts[1], pts[0]))
                t2 = _norm(_sub(pts[-2], pts[-1]))
                for c in fit(pts, t1, t2):
                    pen.curveTo(c[1], c[2], c[3])
        pen.closePath()
    return out
