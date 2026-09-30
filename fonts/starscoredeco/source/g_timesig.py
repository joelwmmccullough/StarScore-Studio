from kit import *
from shapes import *
from registry import add
import registry
from digits import figs

F = figs()
SB = 0.08
for d in "0123456789":
    p = move(F[d], SB, 0)
    add(f"timeSig{d}", p, adv=bounds(p)[2] + SB)


def common(cut=False):
    W, H, T, t = 1.40, 2.0, 0.44, 0.15
    outer = ellipse(W / 2 + 0.04, 0, W / 2 + 0.04, 1.0, 0, 0.66)
    inner = ellipse(W / 2 + 0.04 + 0.05, 0, W / 2 + 0.04 - T, 1.0 - t, 0, 0.66)
    c = diff(outer, inner)
    # open on the right with flat vertical cuts
    c = diff(c, rect(W - 0.28, -0.52, 3, 0.52))
    if cut:
        c = union(c, rect(W / 2 - 0.04, -1.45, W / 2 + 0.14, 1.45))
    return move(c, SB, 0)

cm = common()
add("timeSigCommon", cm, adv=bounds(cm)[2] + SB)
ct = common(True)
add("timeSigCutCommon", ct, adv=bounds(ct)[2] + SB)
pl = move(union(rect(0, -0.12, 1.6, 0.12), rect(0.65, -0.8, 0.95, 0.8)), SB, 0)
add("timeSigPlus", pl, adv=1.6 + 2 * SB)
add("timeSigPlusSmall", scale(pl, 0.6), adv=0.9 + 2 * SB)
add("timeSigFractionalSlash", polyline([(0.1, -1.0), (1.1, 1.0)], 0.2))
add("timeSigParensLeft", move(scale(registry.G["accidentalParensLeft"]["path"], 1.0, 0.85), SB, 0))
add("timeSigParensRight", move(scale(registry.G["accidentalParensRight"]["path"], 1.0, 0.85), SB, 0))

# tuplet figures: 1.5 sp tall, sitting on the baseline
Ft = figs(H=1.5, T=0.34, t=0.13, W=1.06)
for d in "0123456789":
    p = move(Ft[d], 0.04, 0.75)
    add(f"tuplet{d}", p, adv=bounds(p)[2] + 0.04)
add("tupletColon", union(term(0.2, 0.35, 0.3), term(0.2, 1.05, 0.3)))

# octave clef figures
F8 = figs(H=1.1, T=0.24, t=0.10, W=0.80)
eight = F8['8']
one5 = union(F8['1'], move(F8['5'], bounds(F8['1'])[2] + 0.14, 0))
add("clef8", move(eight, 0, 0.55))
add("clef15", move(one5, 0, 0.55))


def with_fig(src, fig, below=True):
    g = registry.G[src]["path"]
    x0, y0, x1, y1 = bounds(g)
    fx0, fy0, fx1, fy1 = bounds(fig)
    fw = fx1 - fx0
    if src.startswith('g'):
        cx = 1.25
    elif src.startswith('f'):
        cx = 1.0
    else:
        cx = (x0 + x1) / 2
    if below:
        f = move(fig, cx - fw / 2 - fx0, y0 - 0.12 - fy1)
    else:
        f = move(fig, cx - fw / 2 - fx0, y1 + 0.12 - fy0)
    return union(g, f)

add("gClef8vb", with_fig("gClef", eight, True))
add("gClef8va", with_fig("gClef", eight, False))
add("gClef15mb", with_fig("gClef", one5, True))
add("gClef15ma", with_fig("gClef", one5, False))
add("fClef8vb", with_fig("fClef", eight, True))
add("fClef8va", with_fig("fClef", eight, False))
add("fClef15mb", with_fig("fClef", one5, True))
add("fClef15ma", with_fig("fClef", one5, False))
