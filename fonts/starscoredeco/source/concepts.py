"""Clef concepts for review (not in the font).

A  Engraved     - the 0.3 clefs (contrast nib, ball terminals). Lives in g_clefs.py.
B  Constructed  - one even stroke built only from true circles and straight lines: a geometric
                  spiral of shrinking semicircles, a pointed top loop, flat cuts, diamond dots.
C  Twin-line    - B's strokes, each with a hairline running beside it, like the alto clef's
                  thick-and-thin bars.
D  Stencil      - A's clefs with narrow stencil breaks where strokes cross or meet.
"""
import math
import mono as M
from kit import sample, parse_skel, poly, union, diff, rect, move, bounds, ellipse, mirror_y
from shapely.geometry import LineString

W_B = 0.27        # stroke width, constructed clefs
HAIR, GAP = 0.075, 0.075


def diamond(cx, cy, r):
    return M.Polygon([(cx - r, cy), (cx, cy + r), (cx + r, cy), (cx, cy - r)])


# ------------------------------------------------------------------ B skeletons
def g_skel():
    """Returns (main skeleton, stem skeleton) for the constructed G clef."""
    cx, cy, R = 1.50, 0.02, 1.02
    ptop = (2.36, 3.16)
    T = M.tangent_from(ptop, cx, cy, R, R, +1)
    if T[0] > cx:
        T = M.tangent_from(ptop, cx, cy, R, R, -1)
    aT = M.ell_angle(cx, cy, R, R, T) % 360
    # outer circle from the tangent point down the left, round to the top
    outer = M.arc(cx, cy, R, R, aT, 450)
    # then a spiral of semicircles, each smaller, turning inward
    R2 = 0.64
    s2 = M.arc(cx, cy + R - R2, R2, R2, 90, 270)            # top -> left -> bottom
    b2y = cy + R - 2 * R2                                     # bottom of the second semicircle
    R3 = 0.34
    s3 = M.arc(cx, b2y + R3, R3, R3, 270, 400)               # bottom -> right -> up, stop short
    # top loop: the stem rises on a curve to a pointed apex, falls on a curve into the diagonal
    xs = 1.50
    apex = (1.98, 4.64)
    ux, uy = T[0] - ptop[0], T[1] - ptop[1]
    L = math.hypot(ux, uy); ux, uy = ux / L, uy / L
    c2 = (ptop[0] - ux * 0.45, ptop[1] - uy * 0.45)           # arrives in line with the diagonal
    pts = sample(parse_skel([(xs, 3.30), ('C', (xs, 3.95), (1.70, 4.38), apex)]), 0.01)
    pts2 = sample(parse_skel([apex, ('C', (2.42, 4.22), (2.62, 3.72), c2), ('L', ptop)]), 0.01)
    loop = M.chain([(p[0], p[1]) for p in pts], [(p[0], p[1]) for p in pts2])
    main = M.chain(loop, M.line(ptop, T), outer, s2, s3)
    tail_c = (xs - 0.46, -1.72)
    stem = M.chain(M.line((xs, 3.46), (xs, -1.72)),
                   M.arc(tail_c[0], tail_c[1], 0.46, 0.46, 0, -170))
    return main, stem


def f_skel():
    c, R = (1.30, 0.12), 0.86
    start = (c[0] - R, c[1])
    end = (0.24, -2.34)
    T = M.tangent_from(end, c[0], c[1], R, R, +1)
    if T[0] < c[0]:
        T = M.tangent_from(end, c[0], c[1], R, R, -1)
    aT = M.ell_angle(c[0], c[1], R, R, T)
    arc_ = M.arc(c[0], c[1], R, R, 180, aT if aT < 180 else aT - 360)
    return M.chain(arc_, M.line(T, end)), start


def c_bowl_skel():
    c, R = (2.02, 1.24), 0.74
    arc_ = M.arc(c[0], c[1], R, R, 150, -90)
    yb = c[1] - R
    return M.chain(arc_, M.line((c[0], yb), (1.30, yb)), M.line((1.30, yb), (0.98, 0.04)))


# ------------------------------------------------------------------ builders
def hairline_beside(sk, d, side):
    """Hairline parallel to a skeleton at distance d (side: 'left'/'right' of its direction)."""
    ls = LineString(sk).offset_curve(d if side == 'left' else -d, quad_segs=32, join_style='round')
    geoms = [ls] if ls.geom_type == 'LineString' else list(ls.geoms)
    return M.G(*[g.buffer(HAIR / 2, cap_style='flat', quad_segs=16) for g in geoms if g.length > 0.2])


def g_B(twin=False):
    main, stem = g_skel()
    g = M.G(M.buf(main, W_B, join='mitre', mitre=6), M.buf(stem, W_B))
    if twin:
        d = W_B / 2 + GAP + HAIR / 2
        stem_line = [(1.50 - d, 1.75), (1.50 - d, -1.35)]
        g = M.G(g, M.buf(stem_line, HAIR))
        # hairline inside the outer ring of the body (from the bottom round to the top)
        cx, cy, R = 1.50, 0.02, 1.02
        ring_in = M.arc(cx, cy, R - d, R - d, 200, 430)
        g = M.G(g, M.buf(ring_in, HAIR))
    return g


def f_B(twin=False):
    from kit import stroke, Nib, arc as karc
    cc, R = (1.30, 0.12), 0.86
    end = (0.24, -2.34)
    T = M.tangent_from(end, cc[0], cc[1], R, R, +1)
    if T[0] < cc[0]:
        T = M.tangent_from(end, cc[0], cc[1], R, R, -1)
    aT = M.ell_angle(cc[0], cc[1], R, R, T)
    s0, segs = karc(cc[0], cc[1], R, R, 180, aT if aT < 180 else aT - 360)
    body = stroke([s0] + segs + [('L', end)], Nib(W_B, W_B, 1.0), ball0=(0.68, 0.62))
    g = M.G(to_shapely(body), diamond(2.62, 0.5, 0.20), diamond(2.62, -0.5, 0.20))
    if twin:
        d = W_B / 2 + GAP + HAIR / 2
        g = M.G(g, M.buf(M.arc(cc[0], cc[1], R - d, R - d, 140, -20), HAIR))
    return g


def c_B(twin=False):
    bars = M.G(M.box(0, -2.0, 0.50, 2.0), M.box(0.70, -2.0, 0.92, 2.0))
    sk = c_bowl_skel()
    up = M.buf(sk, W_B * 0.92, join='mitre', mitre=6)
    if twin:
        d = W_B * 0.46 + GAP + HAIR / 2
        c, R = (2.02, 1.24), 0.74
        up = M.G(up, M.buf(M.arc(c[0], c[1], R - d, R - d, 120, -80), HAIR))
    low = M.affinity.scale(up, 1, -1, origin=(0, 0))
    return M.G(bars, up, low)


def stencil(g, cuts):
    """Subtract narrow bands: each cut = (x, y, angle_deg, length, width)."""
    for x, y, a, L, w in cuts:
        ux, uy = math.cos(math.radians(a)), math.sin(math.radians(a))
        nx, ny = -uy, ux
        g = g.difference(M.Polygon([(x - ux * L / 2 - nx * w / 2, y - uy * L / 2 - ny * w / 2),
                                    (x + ux * L / 2 - nx * w / 2, y + uy * L / 2 - ny * w / 2),
                                    (x + ux * L / 2 + nx * w / 2, y + uy * L / 2 + ny * w / 2),
                                    (x - ux * L / 2 + nx * w / 2, y - uy * L / 2 + ny * w / 2)]))
    return g


def to_shapely(path):
    """pathops Path -> shapely geometry (even-odd rings rebuilt by containment)."""
    from fontTools.pens.recordingPen import RecordingPen
    from shapely.geometry import Polygon
    import smooth
    rp = RecordingPen()
    path.draw(rp)
    rings, cur = [], []
    for op, args in rp.value:
        if op == 'moveTo':
            cur = [args[0]]
        elif op == 'lineTo':
            cur.append(args[0])
        elif op == 'curveTo':
            p0 = cur[-1]
            c1, c2, p3 = args
            for i in range(1, 9):
                t = i / 8
                mt = 1 - t
                cur.append((mt**3 * p0[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t**3 * p3[0],
                            mt**3 * p0[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t**3 * p3[1]))
        elif op in ('closePath', 'endPath'):
            if len(cur) > 2:
                rings.append(Polygon(cur).buffer(0))
    out = None
    for r in rings:
        out = r if out is None else out.symmetric_difference(r)
    return out


def concepts(registry_G):
    """name -> (label, pathops Path)."""
    import g_clefs as gc
    out = {}
    eng = {"gClef": gc.G_ENGRAVED, "fClef": gc.F_ENGRAVED, "cClef": gc.C_ENGRAVED}
    tw = stencil_twin()
    for key in ("gClef", "fClef", "cClef"):
        out[f"concept-{key}-A"] = (f"{key} · concept A: Engraved (0.3)", eng[key])
        out[f"concept-{key}-E"] = (f"{key} · concept E: Stencil + twin-line", M.to_path(tw[key]))
    return out


def halo(g, mask, gap=GAP, hair=HAIR):
    """A hairline following a shape's outline at a small distance, kept only inside mask."""
    ring = g.buffer(gap + hair, quad_segs=32).difference(g.buffer(gap, quad_segs=32))
    return ring.intersection(mask)


def stencil_twin():
    """Concept E: the stencil clefs with twin hairlines."""
    import g_clefs as gc
    body, stem, apx = gc.G_PARTS
    x0 = bounds(gc.gclef())[0]
    gb = to_shapely(body)
    xl = 1.45 - GAP - HAIR / 2                      # hairline left of the stem
    hl = M.buf([(xl, -1.30), (xl, 1.85)], HAIR)
    hl = hl.difference(gb.buffer(gc.GAP_ST))        # over-under, like the stem
    outer = halo(gb, M.box(-1, -0.95, 0.95, 0.95).difference(M.Polygon(M.arc(1.50, 0.05, 1.05, 1.0, 0, 360))))  # echo along the body's outer left side
    gE = M.affinity.translate(M.G(M.affinity.translate(gc.G_STENCIL_SH, x0, 0), hl, outer), -x0, 0)
    fS = gc.F_STENCIL_SH
    fin = halo(fS, M.affinity.translate(M.Polygon(M.arc(1.30, 0.12, 0.66, 0.66, 0, 360)), fS.bounds[0] * 0 - (gc.f_stencil().bounds[0]), 0))
    fE = M.G(fS, fin)
    cS = gc.C_STENCIL_SH
    m1 = M.Polygon(M.arc(1.98, 1.22, 0.60, 0.60, 0, 360))
    m2 = M.affinity.scale(m1, 1, -1, origin=(0, 0))
    cE = M.G(cS, halo(cS, M.G(m1, m2)))
    return {"gClef": gE, "fClef": fE, "cClef": cE}
