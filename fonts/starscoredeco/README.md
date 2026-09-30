# StarScore Deco

An art deco SMuFL music font for StarScore Studio, drawn to go with TT Modernoir
(the Starsign title and heading font). Family "StarScore Deco", with the companion
"StarScore Deco Text" for symbols inside text (tempo marks, dynamics in expressions).

Status: first draft (0.1). Glyphs it doesn't have fall back to Bravura.

## Design rules

- One pen for every stroked glyph: vertical strokes are thick and horizontal strokes thin
  (vertical stress, as in Modernoir).
- Bowls are compass arcs (ellipses and slightly squared ellipses); curves run into straight
  diagonals, like Modernoir's S, 2, 3 and 7 (the treble clef's spine, the bass clef's tail,
  the dynamics s).
- Ends are cut flat. Counters are narrow vertical slots (half and whole noteheads, digits).
- Rests are a lightning-bolt quarter rest and "7"-shaped eighth rests; flags are solid blades.
- Terminal knobs and dots are round discs (the TERMINAL setting in source/shapes.py can
  switch them to oval, diamond or square).
- Metrics follow SMuFL and Bravura: 1 staff space = 250 units, noteheads 1.26 x 1.0 sp,
  left ink at x = 0 and advance = ink width (MuseScore spaces from the outlines).

## Rebuilding

    pip install fonttools skia-pathops
    python3 fonts/starscoredeco/source/build.py

This rewrites StarScoreDeco.otf, StarScoreDecoText.otf and starscoredeco_metadata.json.
Each g_*.py file draws one group of glyphs; kit.py has the drawing tools (pen strokes,
boolean shape operations) and smooth.py fits smooth curves to the stroked outlines.
TT Modernoir itself is not included or copied; the glyphs are drawn from scratch.
