/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscorehouse.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QRegularExpression>

#include "engraving/dom/score.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/box.h"
#include "engraving/dom/text.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/mscore.h"
#include "engraving/style/style.h"
#include "engraving/types/types.h"

using namespace mu::engraving;

namespace mu::project::starscore {
static int starscoreVisibleStaves(const Score* score, bool skipHideWhenEmpty = false)
{
    int n = 0;
    for (const Part* part : score->parts()) {
        if (!part->show()) {
            continue;
        }
        for (const Staff* staff : part->staves()) {
            // The lead sheet's bass staff ("Hide when empty: Always") only shows where it has music, so it
            // doesn't make the lead sheet a keys-sized sheet
            if (skipHideWhenEmpty && staff->hideWhenEmpty() == AutoOnOff::ON) {
                continue;
            }
            if (staff->visible()) {
                ++n;
            }
        }
    }
    return n;
}

double houseStaffHeightMm(const Score* score, bool partBook)
{
    if (partBook) {
        return std::max(1, starscoreVisibleStaves(score, true)) >= 2 ? 6.5 : 7.5;
    }
    const int staves = std::max(1, starscoreVisibleStaves(score));
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

//! The title frame is measured in page view, as it prints. The main score is shown in continuous view, where the
//! frame's texts aren't where they print: Apply Styles measured Bumper Cars' credit 17 mm off and moved it into the
//! music of every Score PDF. Lays the score out in page view for as long as this lives, then goes back.
class PageViewLayout
{
public:
    explicit PageViewLayout(Score* score)
        : m_score(score), m_mode(score ? score->layoutMode() : LayoutMode::PAGE)
    {
        if (m_score && m_mode != LayoutMode::PAGE) {
            m_score->setLayoutMode(LayoutMode::PAGE);
            m_score->setLayoutAll();
            m_score->doLayout();
        }
    }

    ~PageViewLayout()
    {
        if (m_score && m_mode != LayoutMode::PAGE) {
            m_score->setLayoutMode(m_mode);
            m_score->setLayoutAll();
            m_score->doLayout();
        }
    }

private:
    Score* m_score = nullptr;
    LayoutMode m_mode = LayoutMode::PAGE;
};

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
        // No version in the footer yet: put it in the right-hand footer box, as Starsign 2.3 does
        const QString odd = score->style().styleSt(Sid::oddFooterR).toQString();
        starscoreSet(score, Sid::oddFooterR, String::fromQString(odd.isEmpty() ? text : text + "\n" + odd));
        if (score->style().styleB(Sid::footerOddEven)) {
            const QString even = score->style().styleSt(Sid::evenFooterR).toQString();
            starscoreSet(score, Sid::evenFooterR, String::fromQString(even.isEmpty() ? text : text + "\n" + even));
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

    // Multimeasure rests are always on in Starsign scores and parts
    starscoreSet(score, Sid::createMultiMeasureRests, true);
    // H-bar multimeasure rests: horizontal stroke 0.7sp
    starscoreSet(score, Sid::mmRestHBarThickness, Spatium(0.7));

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
        // a fixed height: a frame sized to its contents (MuseScore's default for frames it makes, as in new part
        // books) ignores the height and came out far too tall
        if (first->getProperty(Pid::BOX_AUTOSIZE).toBool()) {
            first->undoChangeProperty(Pid::BOX_AUTOSIZE, false);
        }
        first->undoChangeProperty(Pid::BOX_HEIGHT, Spatium(15.0));
    }

    // Title, subtitle, composer and lyricist follow the style. A size, font or font style set on the text itself
    // (usually from an imported file; Bet's subtitle was 15 pt, also written into the text as <font size="15"/>)
    // overrides the style, so the style's sizes never showed.
    static const QRegularExpression inlineFont("<font\\s+(size|face)=\"[^\"]*\"\\s*/>");
    for (MeasureBase* mb = score->first(); mb && !mb->isMeasure(); mb = mb->next()) {
        if (!mb->isVBox()) {
            continue;
        }
        for (EngravingItem* e : mb->el()) {
            if (!e || !e->isText()) {
                continue;
            }
            Text* t = toText(e);
            const TextStyleType type = t->textStyleType();
            if (type != TextStyleType::TITLE && type != TextStyleType::SUBTITLE && type != TextStyleType::COMPOSER
                && type != TextStyleType::LYRICIST) {
                continue;
            }
            // their place comes from the style too (the subtitle's 16.5 mm, the composer's alignment below)
            if ((type == TextStyleType::SUBTITLE || type == TextStyleType::COMPOSER)
                && t->propertyFlags(Pid::OFFSET) == PropertyFlags::UNSTYLED) {
                t->undoResetProperty(Pid::OFFSET);
            }
            for (Pid p : { Pid::FONT_FACE, Pid::FONT_SIZE, Pid::FONT_STYLE }) {
                if (t->propertyFlags(p) == PropertyFlags::UNSTYLED) {
                    t->undoResetProperty(p);
                }
            }
            QString xml = t->xmlText().toQString();
            const QString clean = QString(xml).remove(inlineFont);
            if (clean != xml) {
                t->undoChangeProperty(Pid::TEXT, String::fromQString(clean));
            }
        }
    }

    // The composer text's last line sits on the subtitle's baseline (it hung a little below it). Measured after
    // layout and corrected through the style's composer offset, so it fits this score's frame and staff size;
    // applying the style again changes nothing once they line up.
    const PageViewLayout pageView(score);
    score->setLayoutAll();
    score->doLayout();
    const Text* sub = nullptr;
    const Text* comp = nullptr;
    for (MeasureBase* mb = score->first(); mb && !mb->isMeasure() && !(sub && comp); mb = mb->next()) {
        if (!mb->isVBox()) {
            continue;
        }
        for (EngravingItem* e : mb->el()) {
            if (e && e->isText()) {
                const Text* t = toText(e);
                if (!sub && t->textStyleType() == TextStyleType::SUBTITLE && !t->empty()) {
                    sub = t;
                } else if (!comp && t->textStyleType() == TextStyleType::COMPOSER && !t->empty()) {
                    comp = t;
                }
            }
        }
    }
    auto baseline = [](const Text* t) {
        const TextBase::LayoutData* ld = t->ldata();
        return ld && !ld->blocks.empty() ? t->pagePos().y() + ld->blocks.back().y() : 0.0;
    };
    if (sub && comp && sub->ldata() && comp->ldata() && !sub->ldata()->blocks.empty() && !comp->ldata()->blocks.empty()) {
        const double delta = baseline(sub) - baseline(comp);   // > 0: the composer goes down
        if (std::abs(delta) > 0.05 * score->style().spatium()) {
            const bool inSpatium = score->style().styleV(Sid::composerOffsetType).toInt() == int(OffsetType::SPATIUM);
            const PointF off = score->style().styleV(Sid::composerOffset).value<PointF>();
            const double step = inSpatium ? delta / score->style().spatium() : delta / DPMM;
            starscoreSet(score, Sid::composerOffset, PointF(off.x(), off.y() + step));
            score->setLayoutAll();
            score->doLayout();
        }
    }

    // A long composer credit must not run into the title, the subtitle or the arrangement label
    clearComposerCredit(score);
}

//! A long composer credit (Balkan Wedding lists many) can reach up into the title or the subtitle once its last line
//! sits on the subtitle's baseline, or into the arrangement label at the top right of a horn sheet ("7-Horn
//! Arrangement", level with the instrument name). It moves down until it clears them by half a staff space, and
//! the title frame grows if the credit would otherwise run into the music.
static void placeComposerCredit(Score* score, bool again);

void clearComposerCredit(Score* score)
{
    placeComposerCredit(score, false);
}

static void placeComposerCredit(Score* score, bool again)
{
    if (!score) {
        return;
    }
    const PageViewLayout pageView(score);
    MeasureBase* frame = nullptr;
    for (MeasureBase* mb = score->first(); mb && !mb->isMeasure(); mb = mb->next()) {
        if (mb->isVBox()) {
            frame = mb;
            break;
        }
    }
    if (!frame) {
        return;
    }
    const Text* comp = nullptr;
    const Text* partName = nullptr;    // the instrument name top left ("Bass Saxophone", "Lead Sheet")
    std::vector<const Text*> others;   // title, subtitle, arrangement label(s)
    const double frameMid = frame->pageBoundingRect().center().x();
    for (EngravingItem* e : frame->el()) {
        if (!e || !e->isText() || toText(e)->empty()) {
            continue;
        }
        const Text* t = toText(e);
        const TextStyleType type = t->textStyleType();
        if (type == TextStyleType::COMPOSER) {
            if (!comp) {
                comp = t;
            }
        } else if (type == TextStyleType::TITLE || type == TextStyleType::SUBTITLE) {
            others.push_back(t);
        } else if (type == TextStyleType::INSTRUMENT_EXCERPT) {
            // the arrangement label is the one on the right, whether aligned right or moved there by hand
            const bool right = t->position() == AlignH::RIGHT
                               || (t->ldata() && t->pageBoundingRect().center().x() > frameMid);
            if (right) {
                others.push_back(t);
            } else if (!partName) {
                partName = t;
            }
        }
    }
    if (!comp || !comp->ldata()) {
        return;
    }
    const double gap = 0.5 * score->style().spatium();
    // Below the arrangement label: 0.64 of a credit line between their outlines. The outlines include space above
    // and below the letters, so this prints as about 6 pt of white space between "7-Horn Arrangement" and the first
    // composer (Joel's choice: Balkan Wedding's Bari Sax, Bass Sax and Bass Clarinet sheets in 1.15.5). Measured
    // on the Linux test build with TT Modernoir: 0.06 of a line printed the two 3 pt into each other.
    double labelGap = 1.85 * score->style().spatium();
    {
        const TextBase::LayoutData* ld = comp->ldata();
        if (ld && ld->blocks.size() >= 2) {
            labelGap = 0.64 * (ld->blocks.at(1).y() - ld->blocks.at(0).y());
        }
    }
    // A credit pushed down earlier (against a label that sat lower then, or with a different gap) stays too low
    // unless it can come back up. It starts from its home position (last line on the subtitle's baseline) and is
    // pushed down from there, so the gap below the label is the same on every horn sheet. On a sheet without an
    // arrangement label, a credit placed in the score itself (not by the style) keeps its place unless something
    // collides with it.
    const bool hasLabel = std::any_of(others.begin(), others.end(), [](const Text* t) {
        return t->textStyleType() == TextStyleType::INSTRUMENT_EXCERPT;
    });
    double up = 0.0;
    // how far the credit is above its house position: its last line on the subtitle's baseline
    auto aboveSubtitle = [&]() -> std::optional<double> {
        const Text* sub = nullptr;
        for (const Text* t : others) {
            if (t->textStyleType() == TextStyleType::SUBTITLE) {
                sub = t;
                break;
            }
        }
        const TextBase::LayoutData* cl = comp->ldata();
        const TextBase::LayoutData* sl = sub ? sub->ldata() : nullptr;
        if (!cl || !sl || cl->blocks.empty() || sl->blocks.empty()) {
            return std::nullopt;
        }
        return (comp->pagePos().y() + cl->blocks.back().y()) - (sub->pagePos().y() + sl->blocks.back().y());
    };
    if (partName && partName->ldata()) {
        // A part score: the credit's last line on the subtitle's baseline, but its first line never higher than
        // the instrument name on the left (a long credit moves down); then below whatever it would run into.
        // Placed the same way every time, whether by the style or in the part score itself.
        const double belowName = comp->pageBoundingRect().top() - partName->pageBoundingRect().top();
        const std::optional<double> sub = aboveSubtitle();
        up = sub ? std::min(*sub, belowName) : belowName;
    } else if (const std::optional<double> sub = aboveSubtitle()) {
        if (comp->propertyFlags(Pid::OFFSET) != PropertyFlags::UNSTYLED) {
            // Placed by the style (a Score): back to its house position, up or down (a credit moved too far down
            // by an earlier version comes back up), but its first line no higher than the top of the frame
            const double belowTop = comp->pageBoundingRect().top() - frame->pageBoundingRect().top();
            up = std::min(*sub, std::max(0.0, belowTop));
        } else if (hasLabel) {
            // placed by hand on a sheet with a label: only ever up to its house position
            up = std::max(0.0, *sub);
        }
    }
    const RectF c = comp->pageBoundingRect().translated(0.0, -up);
    // Each blocker the credit overlaps pushes it below that blocker; repeat, since moving down can meet another
    double down = 0.0;
    for (int pass = 0; pass < 4; ++pass) {
        bool pushed = false;
        for (const Text* other : others) {
            if (!other->ldata()) {
                continue;
            }
            const bool isLabel = other->textStyleType() == TextStyleType::INSTRUMENT_EXCERPT;
            const double g = isLabel ? labelGap : gap;
            const RectF o = other->pageBoundingRect().adjusted(-gap, -gap, gap, g);
            const RectF moved = c.translated(0.0, down);
            // the arrangement label and the credit both sit at the frame's right edge: they meet whenever their
            // heights overlap (their widths aren't compared, so a label measured in the wrong place still counts)
            const bool meets = isLabel ? (moved.top() < o.bottom() && moved.bottom() > o.top()) : moved.intersects(o);
            if (meets && o.bottom() - c.top() > down) {
                down = o.bottom() - c.top();
                pushed = true;
            }
        }
        if (!pushed) {
            break;
        }
    }
    // where the credit ends up: its home position (c) moved down by "down"; "shift" is the move from where it is now
    // (negative: up). A tiny difference is left alone, so nothing changes once it is in place.
    const double shift = down - up;
    if (std::abs(shift) < 0.05 * score->style().spatium()) {
        return;
    }
    const double bottomAfter = c.bottom() + down;
    down = shift;
    if (comp->propertyFlags(Pid::OFFSET) == PropertyFlags::UNSTYLED) {
        // the credit was placed by hand in this score (Balkan Wedding's Bass Trombone part): the style's position
        // doesn't move it, its own does
        Text* c2 = const_cast<Text*>(comp);
        c2->undoChangeProperty(Pid::OFFSET, c2->offset() + PointF(0.0, down), PropertyFlags::UNSTYLED);
    } else {
        const bool inSpatium = score->style().styleV(Sid::composerOffsetType).toInt() == int(OffsetType::SPATIUM);
        const PointF off = score->style().styleV(Sid::composerOffset).value<PointF>();
        starscoreSet(score, Sid::composerOffset, PointF(off.x(), off.y() + (inSpatium ? down / score->style().spatium() : down / DPMM)));
    }
    // the frame grows by what now hangs below it
    bool grew = false;
    if (frame->isVBox()) {
        const double frameBottom = frame->pageBoundingRect().bottom();
        const double overhang = bottomAfter - frameBottom;
        if (overhang > 0.0) {
            const double height = frame->getProperty(Pid::BOX_HEIGHT).value<Spatium>().val();
            frame->undoChangeProperty(Pid::BOX_HEIGHT, Spatium(height + overhang / score->style().spatium() + 0.5));
            grew = true;
        }
    }
    score->setLayoutAll();
    score->doLayout();
    // A credit aligned to the bottom of the frame moves down with it when the frame grows (Balkan Wedding's Bass Sax
    // printed 2.5 pt lower than the other horn sheets): placed once more in the grown frame
    if (grew && !again) {
        placeComposerCredit(score, true);
    }
}

//! The arrangement label (right-positioned instrument-name text) on the instrument name's line: its top level with the
//! instrument name's top. Undoable, so a print-time levelling goes back with the rest of the print-time edits.
bool levelArrangementLabel(Score* score)
{
    MeasureBase* frame = nullptr;
    for (MeasureBase* mb = score ? score->first() : nullptr; mb && !mb->isMeasure(); mb = mb->next()) {
        if (mb->isVBox()) {
            frame = mb;
            break;
        }
    }
    if (!frame) {
        return false;
    }
    Text* ref = nullptr;
    std::vector<Text*> labels;
    for (EngravingItem* e : frame->el()) {
        if (!e || !e->isText() || toText(e)->textStyleType() != TextStyleType::INSTRUMENT_EXCERPT) {
            continue;
        }
        Text* t = toText(e);
        if (t->position() == AlignH::RIGHT) {
            labels.push_back(t);
        } else if (!ref) {
            ref = t;
        }
    }
    if (!ref || labels.empty() || !ref->ldata()) {
        return false;
    }
    bool moved = false;
    const double refTop = ref->pagePos().y() + ref->ldata()->bbox().top();
    for (Text* label : labels) {
        if (!label->ldata()) {
            continue;
        }
        const double dy = refTop - (label->pagePos().y() + label->ldata()->bbox().top());
        if (std::abs(dy) > 0.01) {
            label->undoChangeProperty(Pid::OFFSET, label->offset() + PointF(0.0, dy), PropertyFlags::UNSTYLED);
            moved = true;
        }
    }
    if (moved) {
        score->setLayoutAll();
        score->doLayout();
    }
    return moved;
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
