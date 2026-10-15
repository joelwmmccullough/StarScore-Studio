/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — scale and mode names as chord symbols (Joel, 15 Oct 2026).
 *
 * A chord symbol whose text after the root is a mode name, typed without spaces ("Blocrian", "Charmonicminor",
 * "Daltered", "F#mixolydianb6"), is written as the root followed by the name with its spaces and accidentals:
 * "B locrian", "C harmonic minor", "D altered", "F♯ mixolydian ♭6". The root is an ordinary chord root, so it
 * transposes with the part; the name never changes.
 *
 * The names: the seven modes of the natural minor (major) scale, of the harmonic minor scale and of the melodic minor
 * scale, under their usual names, plus alterations written after a name ("locrian#6", "dorianb2", "lydian#2",
 * "phrygianb4", "mixolydianb9b13"); and the symmetric scales: half-whole, whole-half, diminished, augmented,
 * whole-tone ("Chalfwhole" -> "C half-whole"). Matching ignores case and hyphens. Words that are also chord qualities on their own ("alt",
 * "major", "minor") are not mode names, so "C7alt" and "Cminor" stay chords.
 */

#pragma once

#include <utility>
#include <vector>

#include "global/types/string.h"

namespace mu::engraving::starscore {
struct ModeName {
    muse::String words;                                          // "harmonic minor"
    std::vector<std::pair<muse::String, muse::String> > alterations;   // ("b", "6"): accidental token ("b", "bb", "#", "##", "natural") and degree
    bool valid() const { return !words.isEmpty(); }
};

//! The mode name in a chord symbol's text after the root ("locrian#6"), or an invalid ModeName
inline ModeName modeName(const muse::String& extension)
{
    // longest names first, so "superlocrian" isn't read as "super" + something
    static const std::vector<std::pair<const char16_t*, const char16_t*> > NAMES {
        { u"ukrainiandorian", u"Ukrainian dorian" },
        { u"phrygiandominant", u"phrygian dominant" },
        { u"lydianaugmented", u"lydian augmented" },
        { u"ionianaugmented", u"ionian augmented" },
        { u"altereddominant", u"altered dominant" },
        { u"aeoliandominant", u"aeolian dominant" },
        { u"lydiandominant", u"lydian dominant" },
        { u"harmonicminor", u"harmonic minor" },
        { u"harmonicmajor", u"harmonic major" },
        { u"melodicminor", u"melodic minor" },
        { u"melodicmajor", u"melodic major" },
        { u"naturalminor", u"natural minor" },
        { u"phrygianmajor", u"phrygian major" },
        { u"superlocrian", u"superlocrian" },
        { u"diminished", u"diminished" },
        { u"augmented", u"augmented" },
        { u"wholetone", u"whole-tone" },
        { u"halfwhole", u"half-whole" },
        { u"wholehalf", u"whole-half" },
        { u"ultralocrian", u"ultralocrian" },
        { u"mixolydian", u"mixolydian" },
        { u"jazzminor", u"jazz minor" },
        { u"phrygian", u"phrygian" },
        { u"altered", u"altered" },
        { u"aeolian", u"aeolian" },
        { u"locrian", u"locrian" },
        { u"ionian", u"ionian" },
        { u"dorian", u"dorian" },
        { u"lydian", u"lydian" },
    };

    ModeName out;
    // (hyphens may be typed or not: "half-whole", "halfwhole")
    muse::String s = extension.trimmed().toLower();
    s.remove(u'-');
    if (s.isEmpty()) {
        return out;
    }
    for (const auto& [key, words] : NAMES) {
        const muse::String k(key);
        if (!s.startsWith(k)) {
            continue;
        }
        // what follows the name: alterations only, each an accidental then a degree ("b6", "#11", "bb7")
        std::vector<std::pair<muse::String, muse::String> > alts;
        size_t i = k.size();
        bool ok = true;
        while (i < s.size()) {
            muse::String acc;
            const char16_t c = s.at(i).unicode();
            if (c == u'b' || c == u'♭') {
                acc = (i + 1 < s.size() && (s.at(i + 1).unicode() == u'b' || s.at(i + 1).unicode() == u'♭')) ? u"bb" : u"b";
            } else if (c == u'#' || c == u'♯') {
                acc = (i + 1 < s.size() && (s.at(i + 1).unicode() == u'#' || s.at(i + 1).unicode() == u'♯')) ? u"##" : u"#";
            } else if (c == u'♮') {
                acc = u"natural";
            } else {
                ok = false;
                break;
            }
            i += (acc == u"bb" || acc == u"##") ? 2 : 1;
            size_t j = i;
            while (j < s.size() && s.at(j).isDigit()) {
                ++j;
            }
            if (j == i || j - i > 2) {
                ok = false;
                break;
            }
            alts.emplace_back(acc, s.mid(i, j - i));
            i = j;
        }
        if (ok) {
            out.words = muse::String(words);
            out.alterations = std::move(alts);
        }
        return out;   // (the longest name that starts the text decides, matched or not)
    }
    return out;
}
}
