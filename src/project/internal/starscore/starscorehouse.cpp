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
#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
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

//! (direct: set without the undo stack and without Score::styleChanged, for applyVersionFooter's direct mode)
static bool starscoreSet(Score* score, Sid id, const PropertyValue& value, bool direct = false)
{
    if (score->style().styleV(id) != value) {
        if (direct) {
            score->style().set(id, value);
        } else {
            score->undoChangeStyleVal(id, value);
        }
        return true;
    }
    return false;
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
            m_laidOut = true;
        }
    }

    //! whether the score was laid out (in page view) by the switch
    bool laidOut() const { return m_laidOut; }

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
    bool m_laidOut = false;
};

bool applyVersionFooter(Score* score, const QString& version, bool direct)
{
    if (!score || version.isEmpty()) {
        return false;
    }
    static const QRegularExpression re("Version\\s+\\d+\\.\\d+\\.\\d+", QRegularExpression::CaseInsensitiveOption);
    const QString text = "Version " + version;

    bool found = false;
    bool changed = false;
    for (Sid id : { Sid::oddFooterL, Sid::oddFooterC, Sid::oddFooterR, Sid::evenFooterL, Sid::evenFooterC, Sid::evenFooterR }) {
        QString v = score->style().styleSt(id).toQString();
        if (re.match(v).hasMatch()) {
            v.replace(re, text);
            changed |= starscoreSet(score, id, String::fromQString(v), direct);
            found = true;
        }
    }
    if (!found) {
        // No version in the footer yet: put it in the right-hand footer box, as Starsign 2.3 does
        const QString odd = score->style().styleSt(Sid::oddFooterR).toQString();
        changed |= starscoreSet(score, Sid::oddFooterR, String::fromQString(odd.isEmpty() ? text : text + "\n" + odd), direct);
        if (score->style().styleB(Sid::footerOddEven)) {
            const QString even = score->style().styleSt(Sid::evenFooterR).toQString();
            changed |= starscoreSet(score, Sid::evenFooterR, String::fromQString(even.isEmpty() ? text : text + "\n" + even), direct);
        }
    }
    changed |= starscoreSet(score, Sid::showFooter, true, direct);
    changed |= starscoreSet(score, Sid::footerFirstPage, true, direct);
    return changed;
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
    if (!pageView.laidOut()) {   // (a score shown in continuous view was just laid out by the switch to page view)
        score->setLayoutAll();
        score->doLayout();
    }
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

QStringList arrangementLabelVariants(const QString& label)
{
    QStringList out { label };
    static const QRegularExpression horns("\\b(\\d+)-Horn\\b");
    QString shortForm = QString(label).replace(horns, "\\1H");
    QStringList words = shortForm.split(' ', Qt::SkipEmptyParts);
    auto add = [&](const QString& v) {
        if (!v.isEmpty() && !out.contains(v)) {
            out << v;
        }
    };
    add(shortForm);
    if (words.size() >= 2) {
        add(words.first() + "\n" + words.mid(1).join(' '));
        add(words.mid(0, words.size() - 1).join(' ') + "\n" + words.last());
    }
    return out;
}

std::pair<QString, QString> splitArrangementLabel(const QString& label)
{
    const int nl = label.indexOf('\n');
    return nl < 0 ? std::make_pair(label, QString()) : std::make_pair(label.left(nl), label.mid(nl + 1));
}

static double starscoreSmallSize(double size)
{
    return std::max(5.0, std::round(size * 0.75 * 2.0) / 2.0);
}

String arrangementLabelXml(const QString& main, const QString& note, double size)
{
    QString xml = main.toHtmlEscaped();
    if (!note.isEmpty()) {
        xml += QString("\n<font size=\"%1\"/>").arg(starscoreSmallSize(size)) + note.toHtmlEscaped();
    }
    return String::fromQString(xml);
}

//! The first line of an instrument name's mute lines: "with Harmon Mute", or shortened, "w/ Harmon"
static bool starscoreIsMuteLine(const QString& line)
{
    return line.startsWith("with ") || line.startsWith("w/ ");
}

//! The mute lines at a shortening step (Joel, 17 Oct 2026): 0 as written ("with Harmon Mute" / "and Plunger"), 1 with
//! "w/" and "&" ("w/ Harmon Mute" / "& Plunger"), 2 without the word "Mute" ("w/ Harmon" / "& Plunger"). A line already
//! shortened stays as it is.
static QString starscoreMuteForm(const QString& lines, int step)
{
    if (step <= 0 || lines.isEmpty()) {
        return lines;
    }
    QStringList out;
    for (QString line : lines.split('\n')) {
        if (line.startsWith("with ")) {
            line = "w/ " + line.mid(5);
        } else if (line.startsWith("and ")) {
            line = "& " + line.mid(4);
        }
        if (step >= 2) {
            static const QRegularExpression muteWord(" Mute(,?)$");
            static const QRegularExpression hat(" \\(Hat\\)(,?)$");
            line.replace(muteWord, "\\1");
            line.replace(hat, "\\1");
        }
        out << line;
    }
    return out.join('\n');
}

static constexpr int STARSCORE_MUTE_STEPS = 2;

String instrumentNameXml(const QString& plain, double size)
{
    QStringList lines = plain.split('\n');
    int mutesAt = -1;
    for (int i = 1; i < lines.size(); ++i) {
        if (starscoreIsMuteLine(lines.at(i))) {
            mutesAt = i;
            break;
        }
    }
    if (mutesAt < 0) {
        return String::fromQString(plain.toHtmlEscaped());
    }
    const QString name = lines.mid(0, mutesAt).join('\n');
    const QString mutes = lines.mid(mutesAt).join('\n');
    return String::fromQString(name.toHtmlEscaped() + QString("\n<font size=\"%1\"/>").arg(starscoreSmallSize(size))
                               + mutes.toHtmlEscaped());
}

bool instrumentNameShows(const Text* name, const QString& plain)
{
    if (!name) {
        return false;
    }
    auto flat = [](QString t) { return t.replace('\n', ' ').simplified(); };
    // the name as given, or a shorter form of it (fitInstrumentName), with the same lines under it
    const int nl = plain.indexOf('\n');
    const QString rest = nl < 0 ? QString() : plain.mid(nl);
    const QString shown = flat(name->plainText().toQString());
    bool same = false;
    for (const QString& v : instrumentNameVariants(nl < 0 ? plain : plain.left(nl))) {
        for (int step = 0; step <= STARSCORE_MUTE_STEPS; ++step) {
            same |= shown == flat(v + starscoreMuteForm(rest, step));
        }
    }
    if (!same) {
        return false;
    }
    // with mute lines, they must be the smaller size already (sheets titled before 1.18.54 weren't)
    const bool hasMutes = plain.contains("\nwith ");
    return !hasMutes || name->xmlText().contains(u"<font size=");
}

QString arrangementLabelNote(const Text* label)
{
    if (!label || !label->xmlText().contains(u"<font size=")) {
        return QString();
    }
    const QString plain = label->plainText().toQString();
    const int nl = plain.lastIndexOf('\n');
    return nl < 0 ? QString() : plain.mid(nl + 1);
}

bool fitArrangementLabel(Score* score)
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
    Text* label = nullptr;
    std::vector<const Text*> titles;
    for (EngravingItem* e : frame->el()) {
        if (!e || !e->isText()) {
            continue;
        }
        Text* t = toText(e);
        if (t->textStyleType() == TextStyleType::INSTRUMENT_EXCERPT && t->position() == AlignH::RIGHT) {
            if (!label) {
                label = t;
            }
        } else if (t->textStyleType() == TextStyleType::TITLE || t->textStyleType() == TextStyleType::SUBTITLE) {
            titles.push_back(t);
        }
    }
    if (!label || titles.empty()) {
        return false;
    }
    const double gap = 0.5 * score->style().spatium();
    auto overlaps = [&]() {
        if (!label->ldata() || label->plainText().isEmpty()) {
            return false;
        }
        const RectF l = label->pageBoundingRect().adjusted(-gap, 0.0, 0.0, 0.0);
        for (const Text* t : titles) {
            if (t->ldata() && !t->plainText().isEmpty() && l.intersects(t->pageBoundingRect())) {
                return true;
            }
        }
        return false;
    };
    if (!overlaps()) {
        return false;
    }
    // the forms after the one shown now (its smaller second line, "with 2 trumpets", stays as it is)
    const QString note = arrangementLabelNote(label);
    QString current = label->plainText().toQString();
    if (!note.isEmpty()) {
        current = current.left(current.lastIndexOf('\n'));
    }
    const QStringList variants = arrangementLabelVariants(QString(current).replace('\n', ' ').simplified());
    bool changed = false;
    for (int i = std::max(1, int(variants.indexOf(current)) + 1); i < variants.size() && overlaps(); ++i) {
        label->undoChangeProperty(Pid::TEXT, arrangementLabelXml(variants[i], note, label->size()));
        changed = true;
        score->setLayoutAll();
        score->doLayout();
        levelArrangementLabel(score);
    }
    return changed;
}

//! The instrument's name broken over two lines: "Contrabass Clarinet\nin B♭", then before the last word, the last two…
static QStringList starscoreBrokenNames(const QString& firstLine)
{
    QStringList out { firstLine };
    auto add = [&](const QString& v) {
        if (!v.isEmpty() && !out.contains(v)) {
            out << v;
        }
    };
    const int in = firstLine.lastIndexOf(" in ");
    if (in > 0) {
        add(firstLine.left(in) + "\n" + firstLine.mid(in + 1));
    }
    const QStringList words = firstLine.split(' ', Qt::SkipEmptyParts);
    for (int k = int(words.size()) - 1; k >= 1; --k) {
        add(words.mid(0, k).join(' ') + "\n" + words.mid(k).join(' '));
    }
    return out;
}

//! Each instrument's name and its shorter forms, longest first (Joel, 17 Oct 2026: "Soprano Saxophone -> Soprano Sax
//! -> Sop Sax -> Sop"). A name already shortened finds its place in its list and goes on from there.
static const std::vector<QStringList>& starscoreNameForms()
{
    static const std::vector<QStringList> FORMS {
        { "Soprano Saxophone", "Soprano Sax", "Sop Sax", "Sop" },
        { "Alto Saxophone", "Alto Sax", "Alto" },
        { "Tenor Saxophone", "Tenor Sax", "Ten Sax", "Ten" },
        { "Baritone Saxophone", "Bari Sax", "Bari" },
        { "Bass Saxophone", "Bass Sax" },
        { "Saxophone", "Sax" },                     // "E♭ Saxophone", "B♭ Saxophone"
        { "Trumpet", "Tpt" },
        { "Flugelhorn", "Flgh" },
        { "Contrabass Clarinet", "Cb Clarinet", "Cb Cl" },
        { "Bass Clarinet", "Bass Clar", "Bass Cl" },
        { "Clarinet", "Clar", "Cl" },
        { "Bass Trombone", "Bass Tbn" },
        { "Trombone", "Tbn" },
        { "Alto Flute", "Alto Fl" },
        { "Flute", "Fl" },
        { "Piccolo", "Picc" },
        { "Oboe", "Ob" },
        { "Contrabassoon", "Cbsn" },
        { "Bassoon", "Bsn" },
        { "Horn", "Hn" },
        { "Euphonium", "Euph" },
        { "Violin", "Vln" },
        { "Viola", "Vla" },
        { "Cello", "Vc" },
        { "Electric Guitar", "Guitar", "Gtr" },
        { "Guitar", "Gtr" },
        { "Bass Guitar", "Bass" },
        { "Electric Bass", "Bass" },
        { "Piano", "Pno" },
        { "Percussion", "Perc" },
        { "Vibraphone", "Vibes" },
    };
    return FORMS;
}

QStringList instrumentNameVariants(const QString& firstLine)
{
    // "Trumpet 1 in B♭": the instrument, its number, its key after it ("in B♭") or before it ("E♭ Saxophone")
    static const QRegularExpression KEY_AFTER(QString::fromUtf8("^(.+) in ([A-G][♭♯]?)$"));
    static const QRegularExpression KEY_BEFORE(QString::fromUtf8("^([A-G][♭♯]?) (.+)$"));
    static const QRegularExpression NUMBER("^(.+?)( \\d+)$");
    QString base = firstLine.simplified();
    QString keyAfter, keyBefore, number;
    if (const QRegularExpressionMatch m = KEY_AFTER.match(base); m.hasMatch()) {
        base = m.captured(1);
        keyAfter = m.captured(2);
    } else if (const QRegularExpressionMatch b = KEY_BEFORE.match(base); b.hasMatch()) {
        keyBefore = b.captured(1) + " ";
        base = b.captured(2);
    }
    if (const QRegularExpressionMatch m = NUMBER.match(base); m.hasMatch()) {
        base = m.captured(1);
        number = m.captured(2);
    }
    QStringList shorter;
    bool known = false;
    for (const QStringList& forms : starscoreNameForms()) {
        const int at = int(forms.indexOf(base));
        if (at >= 0) {
            shorter = forms.mid(at + 1);
            known = true;
            break;
        }
    }
    if (!known && keyAfter.isEmpty()) {
        // a name with no shorter form: broken over two lines, as before 1.18.55
        return starscoreBrokenNames(firstLine);
    }
    QStringList out { firstLine };
    auto add = [&](const QString& v) {
        if (!out.contains(v)) {
            out << v;
        }
    };
    if (!keyAfter.isEmpty()) {
        // "Trumpet in B♭" -> "B♭ Trumpet" -> "Trumpet" -> "Tpt"
        add(keyAfter + " " + base + number);
        add(base + number);
    }
    for (const QString& s : shorter) {
        add(keyBefore + s + number);
    }
    // the shortest form still too wide: broken over two lines ("Bass Tbn" -> "Bass" / "Tbn")
    const QStringList broken = starscoreBrokenNames(out.last());
    for (int i = 1; i < broken.size(); ++i) {
        add(broken.at(i));
    }
    return out;
}

//! The title frame's instrument name (top left) and its title and subtitle
static Text* starscoreInstrumentNameText(Score* score, std::vector<const Text*>& titles)
{
    MeasureBase* frame = nullptr;
    for (MeasureBase* mb = score ? score->first() : nullptr; mb && !mb->isMeasure(); mb = mb->next()) {
        if (mb->isVBox()) {
            frame = mb;
            break;
        }
    }
    if (!frame) {
        return nullptr;
    }
    Text* name = nullptr;
    for (EngravingItem* e : frame->el()) {
        if (!e || !e->isText()) {
            continue;
        }
        Text* t = toText(e);
        if (t->textStyleType() == TextStyleType::INSTRUMENT_EXCERPT && t->position() != AlignH::RIGHT) {
            if (!name) {
                name = t;
            }
        } else if (t->textStyleType() == TextStyleType::TITLE || t->textStyleType() == TextStyleType::SUBTITLE) {
            titles.push_back(t);
        }
    }
    return name;
}

static bool starscoreNameOverlaps(const Score* score, const Text* name, const std::vector<const Text*>& titles)
{
    if (!name || !name->ldata() || name->plainText().isEmpty()) {
        return false;
    }
    const double gap = 0.5 * score->style().spatium();
    const RectF r = name->pageBoundingRect().adjusted(0.0, 0.0, gap, 0.0);
    for (const Text* t : titles) {
        if (t->ldata() && !t->plainText().isEmpty() && r.intersects(t->pageBoundingRect())) {
            return true;
        }
    }
    return false;
}

bool instrumentNameOverlapsTitle(Score* score)
{
    std::vector<const Text*> titles;
    const Text* name = starscoreInstrumentNameText(score, titles);
    return starscoreNameOverlaps(score, name, titles);
}

bool fitInstrumentName(Score* score, const QString& plain)
{
    std::vector<const Text*> titles;
    Text* name = starscoreInstrumentNameText(score, titles);
    if (!name || titles.empty()) {
        return false;
    }
    auto overlaps = [&]() {
        return starscoreNameOverlaps(score, name, titles);
    };
    bool changed = false;
    if (!plain.isEmpty()) {
        // a shorter form shown that the name doesn't need (a title made shorter since, or a part's own name that
        // happens to be one, "Trumpet" for "Trumpet in B♭"): the full name first, then shortened only as needed
        auto flat = [](QString t) { return t.replace('\n', ' ').simplified(); };
        if (flat(name->plainText().toQString()) != flat(plain)) {
            name->undoChangeProperty(Pid::TEXT, instrumentNameXml(plain, name->size()));
            changed = true;
            score->setLayoutAll();
            score->doLayout();
            levelArrangementLabel(score);
        }
    }
    if (!overlaps()) {
        return changed;
    }
    // the instrument's own name is the first line, unless it was broken here before: then the first two; the mute
    // lines under it
    const QString current = name->plainText().toQString();
    QStringList lines = current.split('\n');
    QString first = lines.takeFirst();
    if (!lines.isEmpty() && !starscoreIsMuteLine(lines.first())) {
        first += " " + lines.takeFirst();   // ("Contrabass Clarinet" / "in B♭" from before)
    }
    const QString mutes = lines.join('\n');
    const QStringList variants = instrumentNameVariants(first.simplified());
    const QString shownName = current.left(current.size() - (mutes.isEmpty() ? 0 : mutes.size() + 1));
    int ni = std::max(0, int(variants.indexOf(shownName)));
    int muteStep = 0;
    while (starscoreMuteForm(mutes, muteStep + 1) == starscoreMuteForm(mutes, muteStep) && muteStep < STARSCORE_MUTE_STEPS) {
        ++muteStep;   // (already as short as that)
    }
    // the line that reaches furthest right is shortened first: the name, or the mute lines (Joel, 17 Oct 2026)
    auto widest = [&](int fromRow, int toRow) {
        double w = 0.0;
        const auto* ld = name->ldata();
        for (int r = fromRow; ld && r < toRow && r < int(ld->blocks.size()); ++r) {
            w = std::max(w, ld->blocks[r].boundingRect().width());
        }
        return w;
    };
    while (overlaps()) {
        int nextMute = muteStep + 1;
        while (nextMute <= STARSCORE_MUTE_STEPS && starscoreMuteForm(mutes, nextMute) == starscoreMuteForm(mutes, muteStep)) {
            ++nextMute;
        }
        const bool canName = ni + 1 < int(variants.size());
        const bool canMute = !mutes.isEmpty() && nextMute <= STARSCORE_MUTE_STEPS;
        if (!canName && !canMute) {
            break;
        }
        const int nameRows = int(variants[ni].count('\n')) + 1;
        // (the widest line as short as it goes: the other one shortened wouldn't clear the title)
        const bool shortenMutes = !mutes.isEmpty() && widest(nameRows, 1000) > widest(0, nameRows);
        if (shortenMutes ? !canMute : !canName) {
            break;
        }
        if (shortenMutes) {
            muteStep = nextMute;
        } else {
            ++ni;
        }
        const QString m = starscoreMuteForm(mutes, muteStep);
        name->undoChangeProperty(Pid::TEXT, instrumentNameXml(variants[ni] + (m.isEmpty() ? QString() : "\n" + m), name->size()));
        changed = true;
        score->setLayoutAll();
        score->doLayout();
        levelArrangementLabel(score);
    }
    return changed;
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
namespace {
//! The first text of that style in the score's title frames
mu::engraving::Text* frameText(const mu::engraving::Score* score, mu::engraving::TextStyleType type)
{
    for (mu::engraving::MeasureBase* mb = score->first(); mb && !mb->isMeasure(); mb = mb->next()) {
        if (!mb->isVBox()) {
            continue;
        }
        for (mu::engraving::EngravingItem* e : mb->el()) {
            if (e && e->isText() && mu::engraving::toText(e)->textStyleType() == type) {
                return mu::engraving::toText(e);
            }
        }
    }
    return nullptr;
}
}

QString creditTexts(const mu::engraving::Score* score)
{
    QString out;
    for (mu::engraving::TextStyleType type : { mu::engraving::TextStyleType::COMPOSER, mu::engraving::TextStyleType::LYRICIST }) {
        const mu::engraving::Text* t = score ? frameText(score, type) : nullptr;
        out += (t ? t->xmlText().toQString() : QString()) + QChar(0x1F);
    }
    return out;
}

QStringList composerNames(const mu::engraving::Score* score)
{
    const mu::engraving::Text* t = score ? frameText(score, mu::engraving::TextStyleType::COMPOSER) : nullptr;
    if (!t) {
        return {};
    }
    QString text = t->plainText().toQString();
    text.replace(QChar(0x2028), '\n').replace(QChar(0x2029), '\n').replace('\r', '\n');
    // an arranger's credit, on its own line or after the names ("Toshiyuki Sudo(arr. Joel McCullough)")
    static const QRegularExpression arranger(R"((?:\(\s*)?\b(?:(?:Horns?|Strings?|Rhythm|Band)\s+)?(?:arr\.|arr\b|arranged\b|arrangement\b).*$)",
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression splitter(R"(\s+/\s+|\s+&\s+|,\s*|;\s*|\s+and\s+|\s{2,}|\t)");
    QStringList names;
    for (QString line : text.split('\n')) {
        line.remove(arranger);
        for (QString name : line.split(splitter)) {
            name = name.trimmed();
            name.remove(QRegularExpression(R"(^[(\[]+|[)\]]+$)"));
            name = name.trimmed();
            if (name.isEmpty() || name.compare("Composer / arranger", Qt::CaseInsensitive) == 0
                || name.compare("Composer", Qt::CaseInsensitive) == 0 || name.compare("arranger", Qt::CaseInsensitive) == 0) {
                continue;
            }
            if (!names.contains(name)) {
                names << name;
            }
        }
    }
    return names;
}

const mu::engraving::Text* titleFrameText(const mu::engraving::Score* score, mu::engraving::TextStyleType type)
{
    return score ? frameText(score, type) : nullptr;
}

QString songTitle(const mu::engraving::Score* score)
{
    const mu::engraving::Text* t = score ? frameText(score, mu::engraving::TextStyleType::TITLE) : nullptr;
    QString title = t ? t->plainText().toQString().simplified() : QString();
    if (title.isEmpty() && score) {
        title = score->metaTag(u"workTitle").toQString().simplified();
    }
    return title;
}

QString joinNames(const QStringList& names)
{
    if (names.size() <= 1) {
        return names.value(0);
    }
    return names.mid(0, names.size() - 1).join(", ") + " and " + names.back();
}

//! The names with a full stop after them, unless the last already ends in one ("Ray Parker Jr.")
static QString sentenceEnd(const QString& s)
{
    return s.endsWith('.') ? s : s + '.';
}

QString copyrightNotice(const QString& title, const QStringList& composers, bool cover, int year)
{
    static const QString JOEL = "Joel McCullough";
    QStringList others;
    bool joel = false;
    for (const QString& c : composers) {
        if (c.compare(JOEL, Qt::CaseInsensitive) == 0) {
            joel = true;
        } else {
            others << c;
        }
    }
    if (cover) {
        // (Joel in a cover's composer text is its arranger: "Orkestar MÉZÉ / Joel McCullough")
        if (others.isEmpty()) {
            return QString();
        }
        return QString("“%1” by %2 All rights in the composition belong to its copyright owners. Arranged by Joel "
                       "McCullough for Starsign’s own performance use. Not for sale or distribution.")
               .arg(title, sentenceEnd(joinNames(others)));
    }
    if (others.isEmpty() && !joel) {
        return QString();
    }
    // a co-written song: the other composers first (Joel, 21 Oct 2026: they usually had the idea first), then Joel,
    // then Starsign
    QStringList owners = others;
    if (joel) {
        owners << JOEL;
    }
    if (!others.isEmpty()) {
        owners << "Starsign";
    }
    return QString("© %1 %2 All rights reserved. No part of this music may be copied, arranged, recorded or distributed "
                   "without permission.").arg(year).arg(sentenceEnd(joinNames(owners)));
}

int syncCreditTexts(mu::engraving::MasterScore* master, bool apply)
{
    if (!master) {
        return 0;
    }
    int count = 0;
    for (mu::engraving::TextStyleType type : { mu::engraving::TextStyleType::COMPOSER, mu::engraving::TextStyleType::LYRICIST }) {
        const mu::engraving::Text* from = frameText(master, type);
        if (!from) {
            continue;
        }
        const muse::String want = from->xmlText();
        for (mu::engraving::Excerpt* ex : master->excerpts()) {
            mu::engraving::Score* es = ex ? ex->excerptScore() : nullptr;
            mu::engraving::Text* to = es ? frameText(es, type) : nullptr;
            if (!to || to->xmlText() == want) {
                continue;
            }
            ++count;
            if (apply) {
                // (this text only: a linked one already shows the main score's)
                to->undoChangeProperty(mu::engraving::Pid::TEXT, want);
                es->setLayoutAll();
            }
        }
    }
    return count;
}
}
