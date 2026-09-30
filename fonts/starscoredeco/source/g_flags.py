from kit import *
from shapes import *
from registry import add

# Flags hang from the stem end (y=0) with x=0 at the stem's left edge.
# Shape: a thick wedge leaving the stem, a straight diagonal blade, then a short
# curve down to a flat-cut tip (Modernoir's "7").
NIB = Nib(0.13, 0.30, 1.3)
SPACING = 0.84


def one_flag(full=True, L=3.2, root=0.84):
    if full:
        k = L / 3.2
        r = root
        return shape([(0.0, 0.0),
                      ('C', (0.30, -0.36 * k), (1.06, -0.86 * k), (1.08, -1.86 * k)),
                      ('C', (1.09, -2.36 * k), (0.98, -2.80 * k), (0.80, -3.20 * k)),
                      ('L', (0.72, -3.16 * k)),
                      ('C', (0.84, -2.72 * k), (0.88, -2.30 * k), (0.82, -1.98 * k)),
                      ('C', (0.70, -1.46 * k), (0.34, -r - 0.18), (0.0, -r))])
    return shape([(0.0, 0.0),
                  ('C', (0.30, -0.30), (0.98, -0.66), (1.04, -1.34)),
                  ('L', (0.95, -1.36)),
                  ('C', (0.78, -0.98), (0.36, -0.72), (0.0, -0.56))])


def flag_up(n):
    """Returns (path, y of the stem end)."""
    if n == 1:
        return one_flag(True), 0.0
    parts = [move(one_flag(True, 2.44, 0.6), 0, -SPACING)]
    for i in range(n - 1):
        parts.append(move(one_flag(False), 0, SPACING * i))
    return union(*parts), SPACING * (n - 2)


names = {1: "8th", 2: "16th", 3: "32nd", 4: "64th", 5: "128th", 6: "256th"}
for n, nm in names.items():
    up, ytop = flag_up(n)
    add(f"flag{nm}Up", up, anchors={"stemUpNW": (0.0, ytop - 0.04)})
    add(f"flag{nm}Down", mirror_y(up), anchors={"stemDownSW": (0.0, -ytop + 0.04)})

add("flagInternalUp", inter(one_flag(False), rect(0, -1.2, 3, 0.02)))
add("flagInternalDown", mirror_y(inter(one_flag(False), rect(0, -1.2, 3, 0.02))))
