/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscorehouse.h"

#include <QRegularExpression>

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/box.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/mscore.h"
#include "engraving/style/style.h"
#include "engraving/types/types.h"

using namespace mu::engraving;

namespace mu::project::starscore {
static int starscoreVisibleStaves(const Score* score)
{
    int n = 0;
    for (const Part* part : score->parts()) {
        if (!part->show()) {
            continue;
        }
        for (const Staff* staff : part->staves()) {
            if (staff->visible()) {
                ++n;
            }
        }
    }
    return n;
}

double houseStaffHeightMm(const Score* score, bool partBook)
{
    const int staves = std::max(1, starscoreVisibleStaves(score));
    if (partBook) {
        return staves >= 2 ? 6.5 : 7.5;
    }
    if (staves <= 4) {
        return 6.5;
    } else if (staves <= 6) {
        return 6.0;
    } else if (staves <= 10) {
        return 5.5;
    } else if (staves <= 16) {
        return 5.0;
    } else if (staves <= 24) {
        return 4.5;
    }
    return 4.0;
}

static void starscoreSet(Score* score, Sid id, const PropertyValue& value)
{
    if (score->style().styleV(id) != value) {
        score->undoChangeStyleVal(id, value);
    }
}

void applyVersionFooter(Score* score)
{
    if (!score) {
        return;
    }
    starscoreSet(score, Sid::showFooter, true);
    starscoreSet(score, Sid::footerFirstPage, true);
    // $c = copyright on every page ($C is the first page only)
    for (Sid id : { Sid::oddFooterC, Sid::evenFooterC }) {
        String v = score->style().styleSt(id);
        if (v.contains(u"$C")) {
            v.replace(u"$C", u"$c");
        } else if (!v.contains(u"$c")) {
            v = v.isEmpty() ? String(u"$c") : v + u" $c";
        }
        starscoreSet(score, id, v);
    }
}

void applyHouseStyle(Score* score, bool partBook)
{
    if (!score) {
        return;
    }

    // Staff size
    const double mm = houseStaffHeightMm(score, partBook);
    starscoreSet(score, Sid::spatium, mm / 4.0 * DPMM);

    // Chord symbols: the CourseCreator / Songbook design (see chords_starsign.xml)
    starscoreSet(score, Sid::chordSymbolAFontFace, String(u"StarScore Jost"));
    starscoreSet(score, Sid::chordSymbolAFontStyle, int(FontStyle::Normal));
    starscoreSet(score, Sid::chordSymbolAFontSize, 12.0);
    starscoreSet(score, Sid::chordStyle, ChordStylePreset::CUSTOM);
    starscoreSet(score, Sid::chordsXmlFile, false);
    starscoreSet(score, Sid::verticallyStackModifiers, false);
    starscoreSet(score, Sid::chordBassNoteStagger, false);
    starscoreSet(score, Sid::chordBassNoteScale, 1.0);
    starscoreSet(score, Sid::chordExtensionMag, 0.71);
    starscoreSet(score, Sid::chordExtensionAdjust, -0.5);
    starscoreSet(score, Sid::chordModifierMag, 0.71);
    starscoreSet(score, Sid::chordModifierAdjust, -0.5);
    starscoreSet(score, Sid::chordDescriptionFile, String(u"chords_starsign.xml"));

    // Version at the bottom of every page
    applyVersionFooter(score);

    // Title frame on page 1: 15 sp tall
    MeasureBase* first = score->first();
    if (first && first->isVBox()) {
        first->undoChangeProperty(Pid::BOX_HEIGHT, Spatium(15.0));
    }
}

QString versionFromCopyright(const QString& copyright)
{
    static const QRegularExpression re("Version\\s+(\\d+)\\.(\\d+)\\.(\\d+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(copyright);
    return m.hasMatch() ? QString("%1.%2.%3").arg(m.captured(1), m.captured(2), m.captured(3)) : QString();
}

QString copyrightWithVersion(const QString& copyright, const QString& version)
{
    static const QRegularExpression re("Version\\s+\\d+\\.\\d+\\.\\d+", QRegularExpression::CaseInsensitiveOption);
    QString text = copyright;
    if (re.match(text).hasMatch()) {
        text.replace(re, "Version " + version);
        return text;
    }
    return "Version " + version;
}
}
