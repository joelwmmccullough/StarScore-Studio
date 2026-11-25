# StarScore TimeSig

Small fonts for writing a stacked time signature inside a line of text (StarScore 1.38.0: the footnotes that explain
additive time signatures on exported sheets). There is one for each music font StarScore has, so the footnote's time
signatures look like the sheet's: StarScoreTimeSig-<font>.otf, family "StarScore TimeSig <font>" (the name the score's
style uses: Leland, Bravura, Petaluma, StarScore Deco, MuseJazz, Gonville, Emmentaler, Finale Maestro, Finale Broadway).

Their glyphs are the music font's time signature digits and small plus (from Bravura where a font has no small plus),
all under the SIL Open Font License 1.1 (see OFL.txt), with the top and bottom rows of a stacked time signature built in:
Qt doesn't apply Bravura Text's positioning to the plus, and the other text fonts have none.

Made with `python3 mkfont.py` in this folder (fontTools), which also writes their widths to
src/project/internal/starscore/starscoretimesigwidths.h.

| Code points     | Glyphs                                                    |
|-----------------|-----------------------------------------------------------|
| U+F700–U+F709   | 0–9 as the top number (zero width)                        |
| U+F70A          | the small + as part of the top number (zero width)        |
| U+F710–U+F719   | 0–9 as the bottom number, on the baseline (zero width)    |
| U+F720–U+F724   | spacers 250, 125, 50, 25 and 10 units wide (1 em = 1000)  |

One em is the whole time signature's height (four staff spaces); it reaches a quarter of that below the baseline.
