"""The one style sheet every glyph module draws from (measured from TT Modernoir Bold).

Modernoir facts (Bold, cap 700, x-height 525):
  * nearly monoline: O walls 151, O top/bottom 136  -> stress 0.90
  * bowls are true ellipses (superellipse n ~ 2.1), counters are small ovals
  * rounds are wide, uprights narrow: O = 1.01 cap, H = 0.57, S = 0.45, E = 0.42
  * lower case widths / x-height: o .74  n .64  m .93  p .66  s .47  f .47  r .50  z .58
  * figures width / height: 0 .67  1 .34  2 .49  3 .44  4 .62  5 .44  6 .59  7 .48  8 .63  9 .59
  * stem = 0.18 cap = 0.24 x-height
  * diagonals end in sharp points (cut along the horizontal); curves end in flat cuts
"""
from kit import Nib

K = 0.5523          # true ellipse
STRESS = 0.82       # thin / thick. Modernoir is 0.90; a touch more stress reads better at 6 mm staves.


def pen(thick, power=1.0):
    """The one pen: vertical strokes `thick`, horizontals thick*STRESS."""
    return Nib(thick * STRESS, thick, power)


# stroke weights by scale (all in staff spaces)
STEM_TEXT = 0.25    # letters at x-height 1.06  (0.24 x-height, as Modernoir)
STEM_FIG = 0.36     # figures 2 sp tall          (0.18 cap, as Modernoir)
STEM_SYM = 0.30     # clefs, rests, accidentals, ornaments (between the two)
THIN = STRESS       # multiply a stem by this for a horizontal


def thin(thick):
    return thick * STRESS


# Modernoir figure widths as a fraction of height
FIG_W = {'0': .67, '1': .34, '2': .49, '3': .47, '4': .62, '5': .47, '6': .59, '7': .48, '8': .63, '9': .59}
# lower-case widths as a fraction of x-height
LC_W = {'o': .74, 'n': .64, 'm': .93, 'p': .66, 's': .47, 'f': .47, 'r': .50, 'z': .58, 'e': .63, 'a': .60, 'b': .66, 'd': .66}
