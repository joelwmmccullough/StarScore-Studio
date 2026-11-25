# StarScore TimeSig: Bravura's time signature digits and plus (SIL OFL), with the top and bottom rows of a stacked time
# signature baked in, for writing time signatures inside a line of text (the sheet footnotes, StarScore 1.38.0)
import sys
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
src = TTFont(sys.argv[1]); out = sys.argv[2]
cmap = src.getBestCmap(); gs = src.getGlyphSet()
SP = 250  # one staff space (Bravura: 1000 units per em = 4 spaces)
names = [".notdef", "space"]; chars = {0x20: "space"}; cs = {}; adv = {}
def blank(name, width):
    p = T2CharStringPen(width, gs); cs[name] = p.getCharString(); adv[name] = (width, 0)
blank(".notdef", 0); blank("space", SP)
def shifted(name, srcCp, dy):
    p = T2CharStringPen(0, gs)
    gs[cmap[srcCp]].draw(TransformPen(p, (1, 0, 0, 1, 0, dy)))
    cs[name] = p.getCharString(); adv[name] = (0, 0)   # zero width: positioned by the spacers
for d in range(10):
    n, dn = f"num{d}", f"den{d}"
    shifted(n, 0xE080 + d, 3 * SP); names.append(n); chars[0xF700 + d] = n
    shifted(dn, 0xE080 + d, 1 * SP); names.append(dn); chars[0xF710 + d] = dn
shifted("numplus", 0xE08D, 3 * SP); names.append("numplus"); chars[0xF70A] = "numplus"
for cp, w in [(0xF720, 250), (0xF721, 125), (0xF722, 50), (0xF723, 25), (0xF724, 10)]:
    nm = f"sp{w}"; blank(nm, w); names.append(nm); chars[cp] = nm
fb = FontBuilder(1000, isTTF=False)
fb.setupGlyphOrder(names); fb.setupCharacterMap(chars)
fb.setupCFF("StarScoreTimeSig-Regular", {"FullName": "StarScore TimeSig"}, cs, {})
fb.setupHorizontalMetrics(adv)
fb.setupHorizontalHeader(ascent=1050, descent=-150)
fb.setupNameTable({"familyName": "StarScore TimeSig", "styleName": "Regular",
                   "copyright": "Glyphs from Bravura, copyright (c) 2015, Steinberg Media Technologies GmbH. "
                                "Licensed under the SIL Open Font License, Version 1.1."})
fb.setupOS2(sTypoAscender=1050, sTypoDescender=-150, usWinAscent=1050, usWinDescent=150)
fb.setupPost()
fb.save(out)
print("ok", len(names), "glyphs")
