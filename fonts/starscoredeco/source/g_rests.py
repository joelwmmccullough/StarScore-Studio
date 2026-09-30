from kit import *
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
    bolt = polyline([(0.20, 1.50), (0.80, 0.66), (0.26, -0.06), (0.86, -0.78)],
                    [0.17, 0.44, 0.17])
    hook = stroke([(0.86, -0.78),
                   ('C', (0.52, -0.60), (0.14, -0.72), (0.16, -1.04)),
                   ('C', (0.18, -1.30), (0.40, -1.44), (0.64, -1.50))],
                  Nib(0.11, 0.30, 1.2), wfun=lambda s: 1.0 if s < 0.6 else 1.0 - (s - 0.6) * 0.9)
    # flat horizontal cut at top of the bolt
    p = union(bolt, hook)
    p = inter(p, rect(-1, -1.5, 3, 1.5))
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
        dot = term(x - 0.64, y - 0.18, 0.44)
        arm = stroke([(x - 0.66, y - 0.10), ('C', (x - 0.40, y - 0.18), (x - 0.12, y - 0.10), (x + 0.02, y + 0.02))],
                     Nib(0.12, 0.2, 1.3))
        parts += [dot, arm]
    p = union(*parts)
    x0 = bounds(p)[0]
    return move(p, -x0, 0)


for n, name in ((1, "rest8th"), (2, "rest16th"), (3, "rest32nd"), (4, "rest64th"), (5, "rest128th"), (6, "rest256th")):
    add(name, flagged_rest(n))
