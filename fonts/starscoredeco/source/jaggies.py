"""Find likely join artifacts: sharp corners next to very short edges, spikes, slivers."""
import math, json, sys
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

U = 250.0


def contours(glyphset, name):
    r = RecordingPen()
    glyphset[name].draw(r)
    cs, cur = [], None
    for op, args in r.value:
        if op == 'moveTo':
            cur = [('M', args[0])]
        elif op == 'lineTo':
            cur.append(('L', args[0]))
        elif op == 'curveTo':
            cur.append(('C', args))
        elif op in ('closePath', 'endPath'):
            cs.append(cur)
    return cs


def edges(c):
    """List of (p0, p1, tangent_out_at_p0, tangent_in_at_p1, length)."""
    out = []
    start = c[0][1]
    cur = start
    for kind, a in c[1:]:
        if kind == 'L':
            p1 = a
            t = (p1[0] - cur[0], p1[1] - cur[1])
            L = math.hypot(*t)
            out.append((cur, p1, t, t, L))
            cur = p1
        else:
            c1, c2, p1 = a
            t0 = (c1[0] - cur[0], c1[1] - cur[1])
            if math.hypot(*t0) < 1e-6:
                t0 = (c2[0] - cur[0], c2[1] - cur[1])
            t1 = (p1[0] - c2[0], p1[1] - c2[1])
            if math.hypot(*t1) < 1e-6:
                t1 = (p1[0] - c1[0], p1[1] - c1[1])
            # approximate length
            L = (math.hypot(c1[0] - cur[0], c1[1] - cur[1]) + math.hypot(c2[0] - c1[0], c2[1] - c1[1])
                 + math.hypot(p1[0] - c2[0], p1[1] - c2[1]) + math.hypot(p1[0] - cur[0], p1[1] - cur[1])) / 2
            out.append((cur, p1, t0, t1, L))
            cur = p1
    if math.hypot(cur[0] - start[0], cur[1] - start[1]) > 0.5:
        t = (start[0] - cur[0], start[1] - cur[1])
        out.append((cur, start, t, t, math.hypot(*t)))
    return [e for e in out if e[4] > 0.01]


def turn(a, b):
    la, lb = math.hypot(*a), math.hypot(*b)
    if la < 1e-9 or lb < 1e-9:
        return 0.0
    return math.degrees(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]))


def area(c):
    pts = [c[0][1]] + [a if k == 'L' else a[2] for k, a in c[1:]]
    s = 0
    for i in range(len(pts)):
        x0, y0 = pts[i]; x1, y1 = pts[(i + 1) % len(pts)]
        s += x0 * y1 - x1 * y0
    return abs(s) / 2


def scan(otf, short=0.07, min_turn=22, names=None):
    f = TTFont(otf)
    gs = f.getGlyphSet()
    res = {}
    for name in (names or f.getGlyphOrder()):
        if name in ('.notdef', 'space'):
            continue
        hits = []
        for c in contours(gs, name):
            if area(c) / (U * U) < 0.004:
                p = c[0][1]
                hits.append(('tiny contour', p[0] / U, p[1] / U))
                continue
            es = edges(c)
            n = len(es)
            for i in range(n):
                a = es[i]
                b = es[(i + 1) % n]
                ang = abs(turn(a[3], b[2]))
                shortest = min(a[4], b[4]) / U
                if ang > 170:
                    hits.append(('spike %.0f°' % ang, a[1][0] / U, a[1][1] / U))
                elif ang > min_turn and shortest < short:
                    hits.append(('corner %.0f° by %.3f sp edge' % (ang, shortest), a[1][0] / U, a[1][1] / U))
        if hits:
            res[name] = hits
    return res


if __name__ == '__main__':
    r = scan(sys.argv[1] if len(sys.argv) > 1 else 'out/StarScoreDeco.otf')
    tot = sum(len(v) for v in r.values())
    print(len(r), 'glyphs,', tot, 'hits')
    for k, v in sorted(r.items(), key=lambda kv: -len(kv[1])):
        print(f'{k:36s} {len(v):3d}  ', '; '.join(f'{h[0]} @({h[1]:.2f},{h[2]:.2f})' for h in v[:4]))
