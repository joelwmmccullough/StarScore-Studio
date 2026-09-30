"""Build StarScore Deco OTF + SMuFL metadata from the glyph modules."""
import json, sys, importlib, datetime
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
from kit import UNIT, bounds
import registry
import smooth
import os
SMOOTH = os.environ.get('DECO_SMOOTH', '1') == '1'

FAMILY = "StarScore Deco"
TEXT_SB = 0.08
VERSION = "0.2"
MODULES = ["g_noteheads", "g_clefs", "g_accidentals", "g_rests", "g_flags",
           "g_timesig", "g_dynamics", "g_artic", "g_misc", "g_lines"]

HERE = os.path.dirname(os.path.abspath(__file__))
GLYPHNAMES = json.load(open(os.path.join(HERE, '..', '..', 'smufl', 'glyphnames.json')))


def build(out_otf, out_meta, family=FAMILY, text=False):
    registry.G.clear()
    for m in MODULES:
        try:
            mod = importlib.import_module(m)
            importlib.reload(mod)
        except ModuleNotFoundError:
            continue
    G = registry.G
    order = [".notdef", "space"]
    cmap = {32: "space"}
    charstrings = {}
    metrics = {}
    bboxes = {}
    anchors = {}
    # notdef
    pen = T2CharStringPen(500, None)
    charstrings[".notdef"] = pen.getCharString()
    metrics[".notdef"] = (500, 0)
    pen = T2CharStringPen(250, None)
    charstrings["space"] = pen.getCharString()
    metrics["space"] = (250, 0)
    for name, g in G.items():
        cp = GLYPHNAMES[name]["codepoint"]
        code = int(cp[2:], 16)
        path = g["path"]
        if SMOOTH:
            path = smooth.smooth_path(path)
        x0, y0, x1, y1 = bounds(path)
        adv = g["adv"] if g["adv"] is not None else x1
        if text and not name.startswith(("wiggle", "ornamentZigZag")):
            # text font: add side bearings so symbols sit comfortably in running text
            path = smooth.smooth_path(g["path"]) if SMOOTH else g["path"]
            from kit import move as _mv
            path = _mv(path, TEXT_SB, 0)
            adv = adv + 2 * TEXT_SB
            x0 += TEXT_SB
        w = int(round(adv * UNIT))
        pen = T2CharStringPen(w, None)
        path.draw(TransformPen(pen, (UNIT, 0, 0, UNIT, 0, 0)))
        charstrings[name] = pen.getCharString()
        metrics[name] = (w, int(round(x0 * UNIT)))
        order.append(name)
        cmap[code] = name
        bboxes[name] = {"bBoxNE": [round(x1, 3), round(y1, 3)], "bBoxSW": [round(x0, 3), round(y0, 3)]}
        if g["anchors"]:
            anchors[name] = {k: [round(v[0], 3), round(v[1], 3)] for k, v in g["anchors"].items()}

    for u, gname in ((0x266D, "accidentalFlat"), (0x266E, "accidentalNatural"), (0x266F, "accidentalSharp"),
                     (0x2669, "metNoteQuarterUp"), (0x266A, "metNote8thUp")):
        if gname in G:
            cmap[u] = gname
    fb = FontBuilder(1000, isTTF=False)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupCFF(family.replace(" ", ""), {"FullName": family}, charstrings, {})
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=1000, descent=-1000)
    fb.setupNameTable({"familyName": family, "styleName": "Regular",
                       "version": f"Version {VERSION}",
                       "copyright": "Drawn for Joel McCullough / StarScore Studio",
                       "uniqueFontIdentifier": f"{family} {VERSION}", "psName": family.replace(" ", "")})
    fb.setupOS2(sTypoAscender=1000, sTypoDescender=-1000, usWinAscent=1500, usWinDescent=1000,
                sxHeight=250, sCapHeight=500)
    fb.setupPost()
    fb.save(out_otf)

    meta = {
        "fontName": family,
        "fontVersion": float(VERSION),
        "engravingDefaults": registry.ENGRAVING_DEFAULTS,
        "glyphBBoxes": bboxes,
        "glyphsWithAnchors": anchors,
    }
    json.dump(meta, open(out_meta, "w"), indent=1, sort_keys=True)
    return len(G)


if __name__ == "__main__":
    out = os.path.join(HERE, '..')
    build(os.path.join(out, "StarScoreDecoText.otf"), os.path.join(HERE, "text_metadata.tmp.json"), family="StarScore Deco Text", text=True)
    os.remove(os.path.join(HERE, "text_metadata.tmp.json"))
    n = build(os.path.join(out, "StarScoreDeco.otf"), os.path.join(out, "starscoredeco_metadata.json"))
    print("glyphs:", n)
