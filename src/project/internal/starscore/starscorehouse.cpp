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

void applyVersionFooter(Score* score, const QString& version)
{
    if (!score || version.isEmpty()) {
        return;
    }
    static const QRegularExpression re("Version\\s+\\d+\\.\\d+\\.\\d+", QRegularExpression::CaseInsensitiveOption);
    const QString text = "Version " + version;

    bool found = false;
    for (Sid id : { Sid::oddFooterL, Sid::oddFooterC, Sid::oddFooterR, Sid::evenFooterL, Sid::evenFooterC, Sid::evenFooterR }) {
        QString v = score->style().styleSt(id).toQString();
        if (re.match(v).hasMatch()) {
            v.replace(re, text);
            starscoreSet(score, id, String::fromQString(v));
            found = true;
        }
    }
    if (!found) {
        // No version in the footer yet: put it on its own line at the top of the centre footer box, as Starsign 2.5 does
        const QString odd = score->style().styleSt(Sid::oddFooterC).toQString();
        starscoreSet(score, Sid::oddFooterC, String::fromQString(odd.isEmpty() ? text : text + "\n" + odd));
        if (score->style().styleB(Sid::footerOddEven)) {
            const QString even = score->style().styleSt(Sid::evenFooterC).toQString();
            starscoreSet(score, Sid::evenFooterC, String::fromQString(even.isEmpty() ? text : text + "\n" + even));
        }
    }
    starscoreSet(score, Sid::showFooter, true);
    starscoreSet(score, Sid::footerFirstPage, true);
}

void applyHouseStyle(Score* score, bool partBook, const QString& version)
{
    if (!score) {
        return;
    }

    // Staff size
    const double mm = houseStaffHeightMm(score, partBook);
    starscoreSet(score, Sid::spatium, mm / 4.0 * DPMM);

    // Chord symbols (font, size, chords_starsign.xml, superscript sizes) come from the style file (Starsign 2.3+)

    // Version in the footer
    applyVersionFooter(score, version);

    // Subtitle 16.5 mm below the top of the title frame
    const PointF subtitle = score->style().styleV(Sid::subTitleOffset).value<PointF>();
    starscoreSet(score, Sid::subTitleOffsetType, int(OffsetType::ABS));
    starscoreSet(score, Sid::subTitleOffset, PointF(subtitle.x(), 16.5));

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
