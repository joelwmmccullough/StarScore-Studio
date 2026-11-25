# StarScore TimeSig: the time signature digits and small plus of each of StarScore's music fonts (all SIL OFL), with
# the top and bottom rows of a stacked time signature built in, for writing time signatures inside a line of text (the
# sheet footnotes, StarScore 1.38.0). Run from this folder: python3 mkfont.py
# Writes StarScoreTimeSig-<font>.otf for each music font, and the digit widths for StarScore
# (src/project/internal/starscore/starscoretimesigwidths.h).
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen

# the music font's name in a score's style, its file, its licence holder
FONTS = [
    ("Leland", "../leland/Leland.otf", "MuseScore"),
    ("Bravura", "../bravura/Bravura.otf", "Steinberg Media Technologies GmbH"),
    ("Petaluma", "../petaluma/Petaluma.otf", "Steinberg Media Technologies GmbH"),
    ("StarScore Deco", "../starscoredeco/StarScoreDeco.otf", "StarScore Studio"),
    ("MuseJazz", "../musejazz/MuseJazz.otf", "MuseScore"),
    ("Gonville", "../gootville/Gootville.otf", "Simon Tatham, MuseScore"),
    ("Emmentaler", "../mscore/MScore.otf", "MuseScore"),
    ("Finale Maestro", "../finalemaestro/FinaleMaestro.otf", "MakeMusic"),
    ("Finale Broadway", "../finalebroadway/FinaleBroadway.otf", "MakeMusic"),
]
FALLBACK = "../bravura/Bravura.otf"   # a glyph a font doesn't have (Leland has no small plus), as MuseScore does
SP = 250        # one staff space (1000 units per em = 4 spaces = the whole time signature's height)
NUM_DY = 2 * SP  # the top number: two spaces up; the bottom one on the baseline (the digits are centred on it)

fallback = TTFont(FALLBACK)
widths = {}
for styleName, path, holder in FONTS:
    src = TTFont(path)
    scale = 1000 / src["head"].unitsPerEm
    sources = []
    for cp in [0xE080 + d for d in range(10)] + [0xE08D]:
        f = src if cp in src.getBestCmap() else fallback
        sources.append((f, f.getBestCmap()[cp]))
    # (each font's digits brought to two spaces high, centred on their row: Petaluma's are twice the usual size, and
    # MuseScore stacks a time signature by the digits' real height)
    from fontTools.pens.boundsPen import BoundsPen
    def bounds(font, glyph):
        b = BoundsPen(font.getGlyphSet()); font.getGlyphSet()[glyph].draw(b)
        k = 1000 / font["head"].unitsPerEm
        return [v * k for v in b.bounds]
    digitBounds = [bounds(*sources[d]) for d in range(10)]
    lo = min(b[1] for b in digitBounds); hi = max(b[3] for b in digitBounds)
    digitScale = 2 * SP / (hi - lo); digitMid = (hi + lo) / 2
    pb = bounds(*sources[10]); plusMid = (pb[1] + pb[3]) / 2
    plusScale = digitScale if sources[10][0] is src else 1.0
    names = [".notdef", "space"]; chars = {0x20: "space"}; cs = {}; adv = {}
    def blank(name, width):
        p = T2CharStringPen(width, None); cs[name] = p.getCharString(); adv[name] = (width, 0)
    blank(".notdef", 0); blank("space", SP)
    def shifted(name, font, glyph, dy, k, mid):
        s = 1000 / font["head"].unitsPerEm * k
        p = T2CharStringPen(0, None)
        font.getGlyphSet()[glyph].draw(TransformPen(p, (s, 0, 0, s, 0, dy - mid * k)))
        cs[name] = p.getCharString(); adv[name] = (0, 0)
    fw = []
    for d in range(10):
        font, glyph = sources[d]
        shifted(f"num{d}", font, glyph, NUM_DY, digitScale, digitMid); names.append(f"num{d}"); chars[0xF700 + d] = f"num{d}"
        shifted(f"den{d}", font, glyph, 0, digitScale, digitMid); names.append(f"den{d}"); chars[0xF710 + d] = f"den{d}"
        fw.append(round(font["hmtx"][glyph][0] * 1000 / font["head"].unitsPerEm * digitScale))
    font, glyph = sources[10]
    shifted("numplus", font, glyph, NUM_DY, plusScale, plusMid); names.append("numplus"); chars[0xF70A] = "numplus"
    fw.append(round(font["hmtx"][glyph][0] * 1000 / font["head"].unitsPerEm * plusScale))
    for cp, w in [(0xF720, 250), (0xF721, 125), (0xF722, 50), (0xF723, 25), (0xF724, 10)]:
        blank(f"sp{w}", w); names.append(f"sp{w}"); chars[cp] = f"sp{w}"
    family = "StarScore TimeSig " + styleName
    fb = FontBuilder(1000, isTTF=False)
    fb.setupGlyphOrder(names); fb.setupCharacterMap(chars)
    fb.setupCFF(family.replace(" ", "") + "-Regular", {"FullName": family}, cs, {})
    fb.setupHorizontalMetrics(adv)
    fb.setupHorizontalHeader(ascent=800, descent=-300)
    fb.setupNameTable({"familyName": family, "styleName": "Regular",
                       "copyright": f"Glyphs from {styleName} ({holder}) and Bravura (Steinberg Media Technologies "
                                    "GmbH). Licensed under the SIL Open Font License, Version 1.1."})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-300, usWinAscent=800, usWinDescent=300)
    fb.setupPost()
    out = "StarScoreTimeSig-" + styleName.replace(" ", "") + ".otf"
    fb.save(out)
    widths[styleName] = fw
    print(out)

with open("../../src/project/internal/starscore/starscoretimesigwidths.h", "w") as h:
    h.write("// Made by fonts/starscoretimesig/mkfont.py: each StarScore TimeSig font's digit 0-9 and small plus widths\n")
    h.write("// (1 em = 1000 = a time signature's height). Don't edit by hand.\n#pragma once\n\n")
    h.write("struct StarScoreTimeSigWidths {\n    const char16_t* musicFont;\n    int widths[11];\n};\n\n")
    h.write("static const StarScoreTimeSigWidths STARSCORE_TIMESIG_WIDTHS[] = {\n")
    for name, fw in widths.items():
        h.write(f'    {{ u"{name}", {{ {", ".join(str(w) for w in fw)} }} }},\n')
    h.write("};\n")
