G = {}


def add(name, path, adv=None, anchors=None):
    G[name] = {"path": path, "adv": adv, "anchors": anchors or {}}


ENGRAVING_DEFAULTS = {
    "staffLineThickness": 0.13,
    "stemThickness": 0.12,
    "beamThickness": 0.5,
    "beamSpacing": 0.25,
    "legerLineThickness": 0.16,
    "legerLineExtension": 0.35,
    "slurEndpointThickness": 0.07,
    "slurMidpointThickness": 0.22,
    "tieEndpointThickness": 0.07,
    "tieMidpointThickness": 0.22,
    "thinBarlineThickness": 0.16,
    "thickBarlineThickness": 0.5,
    "dashedBarlineThickness": 0.16,
    "dashedBarlineDashLength": 0.5,
    "dashedBarlineGapLength": 0.25,
    "barlineSeparation": 0.4,
    "thinThickBarlineSeparation": 0.4,
    "repeatBarlineDotSeparation": 0.16,
    "bracketThickness": 0.5,
    "subBracketThickness": 0.16,
    "hairpinThickness": 0.14,
    "octaveLineThickness": 0.14,
    "pedalLineThickness": 0.14,
    "repeatEndingLineThickness": 0.14,
    "arrowShaftThickness": 0.16,
    "lyricLineThickness": 0.14,
    "textEnclosureThickness": 0.14,
    "tupletBracketThickness": 0.14,
    "hBarThickness": 0.7,
}
