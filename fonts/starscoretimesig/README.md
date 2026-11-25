# StarScore TimeSig

A small font for writing a stacked time signature inside a line of text (StarScore 1.38.0: the footnotes that explain
additive time signatures on exported sheets). Its glyphs are Bravura's time signature digits and its small plus
(Bravura copyright (c) 2015, Steinberg Media Technologies GmbH, SIL Open Font License 1.1, see OFL.txt), with the top
and bottom rows of a stacked time signature built in, because Qt doesn't apply Bravura Text's positioning to the plus.

Made with `python3 mkfont.py ../bravura/Bravura.otf StarScoreTimeSig.otf` (fontTools).

| Code points     | Glyphs                                                   |
|-----------------|----------------------------------------------------------|
| U+F700–U+F709   | 0–9 as the top number (zero width)                       |
| U+F70A          | the small + as part of the top number (zero width)       |
| U+F710–U+F719   | 0–9 as the bottom number (zero width)                    |
| U+F720–U+F724   | spacers 250, 125, 50, 25 and 10 units wide (1 em = 1000) |

One em is the whole time signature's height (four staff spaces). The digits are positioned by the spacers.
