# StarScore Deco

An art deco SMuFL music font for StarScore Studio, drawn to go with TT Modernoir
(the Starsign title and heading font). Family "StarScore Deco", with the companion
"StarScore Deco Text" for symbols inside text (tempo marks, dynamics in expressions).

Status: first draft (0.1). Glyphs it doesn't have fall back to Bravura.

## Design rules (0.2: one style sheet, source/style.py, measured from TT Modernoir Bold)

- One pen for every stroked glyph: nearly monoline, horizontals 0.82 of verticals (Modernoir is 0.90).
  Stem weights: letters 0.25 sp (x-height 1.06), figures 0.36 sp (height 2), symbols 0.30 sp.
- Bowls are true circles and ellipses with small oval counters; curves run into straight tangent
  diagonals, like Modernoir's S, 2, 3 and 7 (the treble clef's spine, the bass clef's tail,
  the dynamics s).
- Ends are cut flat; diagonals end in sharp points cut along the horizontal. Counters are small ovals or slots.
- Widths follow Modernoir: rounds wide, uprights narrow (figure and letter width tables in style.py).
- Rests are a lightning-bolt quarter rest and "7"-shaped eighth rests; flags are solid blades.
- Ball terminals grow out of their strokes: stroke(..., ball0/ball1=(diameter, ramp)) swells the
  line to the ball's width with a smooth ramp (clef knobs and tails, rest arms, comma). Free dots
  (repeat, staccato, augmentation, bass clef colon) are round discs.
- Metrics follow SMuFL and Bravura: 1 staff space = 250 units, noteheads 1.26 x 1.0 sp,
  left ink at x = 0 and advance = ink width (MuseScore spaces from the outlines).

## Rebuilding

    pip install fonttools skia-pathops shapely
    python3 fonts/starscoredeco/source/build.py

This rewrites StarScoreDeco.otf, StarScoreDecoText.otf and starscoredeco_metadata.json.
Each g_*.py file draws one group of glyphs; kit.py has the drawing tools (pen strokes,
boolean shape operations) and smooth.py fits smooth curves to the stroked outlines.
`source/jaggies.py <font.otf>` lists sharp corners next to very short edges (where shapes meet badly);
what it still reports after 1.11.1 is intentional (double sharp corners, tile edges of wiggle lines,
the inner corners of the 3).
TT Modernoir itself is not included or copied; the glyphs are drawn from scratch.
