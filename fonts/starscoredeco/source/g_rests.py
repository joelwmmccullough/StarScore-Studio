from kit import *
import math
from shapes import *
from registry import add

# Blocks: whole/half rests are plain blocks, wider than a notehead.
add("restWhole", rect(0, -0.52, 1.24, 0.0))
add("restHalf", rect(0, 0.0, 1.24, 0.52))
add("restDoubleWhole", rect(0, 0.0, 0.52, 1.0))
add("restLonga", rect(0, -1.0, 0.52, 1.0))
add("restMaxima", union(rect(0, -1.0, 0.52, 1.0), rect(0.9, -1.0, 1.42, 1.0)))
add("restWholeLegerLine", union(rect(0, -0.52, 1.24, 0.0), rect(-0.4, -0.08, 1.64, 0.08)))
add("restHalfLegerLine", union(rect(0, 0.0, 1.24, 0.52), rect(-0.4, -0.08, 1.64, 0.08)))


# Quarter rest: a lightning bolt (straight zigzag) ending in a small hook.
def quarter():
    top = (0.20, 1.50)
    p1 = (0.80, 0.66)
    ux, uy = p1[0] - top[0], p1[1] - top[1]
    L = math.hypot(ux, uy); ux, uy = ux / L, uy / L
    start = (top[0] - ux * 0.3, top[1] - uy * 0.3)          # run past the top so the cut is clean
    hook_nib = Nib(0.11, 0.30, 1.2)
    corner = (0.86, -0.78)
    hcx, hcy, hrx, hry = 0.44, -1.11, 0.30, 0.33
    top_pt = (hcx, hcy + hry)                                  # top of the hook's arc
    hd = (top_pt[0] - corner[0], top_pt[1] - corner[1])
    Lh = math.hypot(*hd); hd = (hd[0] / Lh, hd[1] / Lh)
    hw = hook_nib.width(*hd)
    bolt = polyline([start, p1, (0.26, -0.06), corner], [0.17, 0.44, 0.17])
    L3 = math.hypot(corner[0] - 0.26, corner[1] + 0.06)
    d3 = ((corner[0] - 0.26) / L3, (corner[1] + 0.06) / L3)
    _, harc = arc(hcx, hcy, hrx, hry, 90, 292)
    hook = stroke([corner, ('L', top_pt)] + harc, hook_nib,
                  wfun=lambda s: 1.0 if s < 0.7 else 1.0 - (s - 0.7) * 1.2)
    wa, wb = 0.17 / 2, hw / 2
    na, nb = (-d3[1], d3[0]), (-hd[1], hd[0])
    bevel = poly_hull([(corner[0] + na[0] * wa, corner[1] + na[1] * wa), (corner[0] - na[0] * wa, corner[1] - na[1] * wa),
                       (corner[0] + nb[0] * wb, corner[1] + nb[1] * wb), (corner[0] - nb[0] * wb, corner[1] - nb[1] * wb)])
    bolt = union(bolt, bevel)
    p = union(bolt, hook)
    p = inter(p, rect(-1, -3, 3, 1.5))                      # flat horizontal cut at the top
    return p


q = quarter()
add("restQuarter", move(q, -bounds(q)[0], 0))


# Eighth rest family: square dot + thin arm + straight diagonal stem.
SLOPE = 0.30   # stem x change per sp
LEVELS = {1: [0], 2: [0, -1], 3: [1, 0, -1], 4: [1, 0, -1, -2], 5: [2, 1, 0, -1, -2],
          6: [2, 1, 0, -1, -2, -3]}
BOTTOM = {1: -1.0, 2: -2.0, 3: -2.0, 4: -3.0, 5: -3.0, 6: -4.0}


def flagged_rest(n):
    levels = LEVELS[n]
    top = max(levels) + 0.62
    bot = BOTTOM[n]
    xtop = 0.96 + SLOPE * (top - 0.62) * 0  # top of stem x
    def sx(y):
        return xtop - SLOPE * (top - y)
    parts = []
    stem = poly([(sx(top) - 0.08, top + 0.02), (sx(top) + 0.10, top + 0.02),
                 (sx(bot) + 0.12, bot), (sx(bot) - 0.10, bot)])
    parts.append(stem)
    for lv in levels:
        y = lv + 0.62
        x = sx(y)
        dot = None
        arm = stroke([(x - 0.64, y - 0.16), ('C', (x - 0.40, y - 0.20), (x - 0.12, y - 0.10), (x + 0.08, y + 0.06))],
                     Nib(0.12, 0.2, 1.3), ball0=(0.50, 0.42))
        # the arm ends exactly on the stem's right edge
        arm = inter(arm, halfplane((sx(bot) + 0.12, bot), (sx(top) + 0.10, top + 0.02)))
        arm = diff(arm, rect(sx(top) - 0.30, top + 0.02, 9, 9))   # flush with the stem's flat top, near the stem only
        parts += [dot, arm]
    p = union(*parts)
    x0 = bounds(p)[0]
    return move(p, -x0, 0)


for n, name in ((1, "rest8th"), (2, "rest16th"), (3, "rest32nd"), (4, "rest64th"), (5, "rest128th"), (6, "rest256th")):
    add(name, flagged_rest(n))
