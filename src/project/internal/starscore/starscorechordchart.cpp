/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — chord charts (see starscorechordchart.h).
 *
 * This is a port of the Python prototype in the owner's scratchpad (extract.py, chart.py, pdfchart.py, render.py).
 * The comments marked "prototype:" keep the reasons the prototype's comments gave; where its behaviour looks odd
 * it is kept on purpose, so that the C++ output can be diffed against it.
 */
#include "starscorechordchart.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/chordlist.h"
#include "engraving/dom/fret.h"
#include "engraving/dom/text.h"
#include "engraving/dom/box.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/barline.h"
#include "engraving/dom/volta.h"
#include "engraving/dom/jump.h"
#include "engraving/dom/marker.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/systemtext.h"
#include "engraving/dom/tempotext.h"
#include "engraving/types/fraction.h"
#include "engraving/types/typesconv.h"

#include "organizer/orgplatform.h"

#include "translation.h"
#include "log.h"

using namespace mu::engraving;
using mu::engraving::Fraction;

namespace mu::project::starscore {
// ============================================================================================ small helpers
namespace {
//! Python's str(Fraction): reduced, "n" when the denominator is 1
QString fracStr(Fraction f)
{
    f.reduce();
    if (f.denominator() == 1) {
        return QString::number(f.numerator());
    }
    return QString("%1/%2").arg(f.numerator()).arg(f.denominator());
}

//! "n/d" or "n" -> Fraction (reduced, as Python's Fraction is)
Fraction parseFrac(const QString& s)
{
    const int slash = s.indexOf('/');
    if (slash < 0) {
        return Fraction(s.trimmed().toInt(), 1);
    }
    return Fraction(s.left(slash).trimmed().toInt(), s.mid(slash + 1).trimmed().toInt()).reduced();
}

//! Python's html.escape(s) (quote=True)
QString esc(const QString& s)
{
    QString r = s;
    r.replace('&', "&amp;");
    r.replace('<', "&lt;");
    r.replace('>', "&gt;");
    r.replace('"', "&quot;");
    r.replace('\'', "&#x27;");
    return r;
}

//! Python's "%.3f"
QString f3(double v)
{
    return QString::number(v, 'f', 3);
}

//! Python's len(): code points, not UTF-16 units
int pyLen(const QString& s)
{
    return int(s.toUcs4().size());
}

//! Python's str.split() (any whitespace, no empty parts)
QStringList pySplit(const QString& s)
{
    static const QRegularExpression ws("\\s+");
    return s.split(ws, Qt::SkipEmptyParts);
}

//! Python's "%(name)s" formatting of the CSS templates: "%%" is one percent sign
QString pyFormat(const QString& tpl, const std::map<QString, QString>& values)
{
    QString out;
    out.reserve(tpl.size());
    for (int i = 0; i < tpl.size(); ++i) {
        if (tpl[i] != '%') {
            out += tpl[i];
            continue;
        }
        if (i + 1 < tpl.size() && tpl[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        if (i + 1 < tpl.size() && tpl[i + 1] == '(') {
            const int close = tpl.indexOf(")s", i + 2);
            if (close > 0) {
                const QString name = tpl.mid(i + 2, close - i - 2);
                auto it = values.find(name);
                out += it != values.end() ? it->second : QString();
                i = close + 1;
                continue;
            }
        }
        out += tpl[i];
    }
    return out;
}

//! urllib.parse.quote(s, safe=":/=,")
QString urlQuote(const QString& s)
{
    static const QByteArray safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-~:/=,";
    const QByteArray utf8 = s.toUtf8();
    QString out;
    for (const char c : utf8) {
        if (safe.contains(c)) {
            out += QChar::fromLatin1(c);
        } else {
            out += "%" + QString("%1").arg(static_cast<unsigned char>(c), 2, 16, QChar('0')).toUpper();
        }
    }
    return out;
}

//! The visible text of an element's <text> as written to the file (extract.py's text_of): tags dropped, <sym>
//! names kept in braces, line-break tags as " / ", entities decoded, stripped
QString textOf(const String& xmlText)
{
    QString s = xmlText.toQString();
    static const QRegularExpression sym("<sym>([^<]*)</sym>");
    static const QRegularExpression br("<br\\s*/>");
    static const QRegularExpression tag("<[^>]+>");
    static const QRegularExpression numEntity("&#(x[0-9A-Fa-f]+|[0-9]+);");
    s.replace(sym, "{\\1}");
    s.replace(br, " / ");
    s.replace('\n', " / ");   // the file writes a line break as <br/>
    s.remove(tag);
    // the XML parser the prototype used decodes every entity; its own replace list is only the three it re-escaped
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    QRegularExpressionMatch m;
    while ((m = numEntity.match(s)).hasMatch()) {
        const QString num = m.captured(1);
        const char32_t cp = num.startsWith('x') ? num.mid(1).toUInt(nullptr, 16) : num.toUInt();
        s.replace(m.capturedStart(), m.capturedLength(), QString::fromUcs4(&cp, 1));
    }
    s.replace("&amp;", "&");
    return s.trimmed();
}

//! Tonal pitch class -> note name ("Eb", "F#"), extract.py's tpc_name
QString tpcName(int tpc)
{
    static const char* STEPS[] = { "F", "C", "G", "D", "A", "E", "B" };
    const int f = tpc - 14 + 1;   // 0 = F
    const int acc = static_cast<int>(std::floor(f / 7.0));
    const int step = ((f % 7) + 7) % 7;
    QString s = QString::fromLatin1(STEPS[step]);
    if (acc > 0) {
        s += QString(acc, '#');
    } else if (acc < 0) {
        s += QString(-acc, 'b');
    }
    return s;
}

QString eidOf(const EngravingItem* e)
{
    const EID id = e->eid();
    return id.isValid() ? QString::fromStdString(id.toStdString()) : QString();
}

QJsonValue optString(const std::optional<QString>& s)
{
    return s ? QJsonValue(*s) : QJsonValue(QJsonValue::Null);
}
}

// ============================================================================================ 1. extraction
QJsonObject ChordChartData::toJson() const
{
    QJsonArray ms;
    for (const ChordChartMeasure& m : measures) {
        QJsonObject o;
        o["i"] = m.i;
        o["ts"] = m.ts;
        o["len"] = m.len;
        o["rm"] = optString(m.rm);
        o["startRepeat"] = m.startRepeat;
        o["endRepeat"] = m.endRepeat;
        if (m.volta) {
            QJsonArray endings;
            for (int e : m.volta->endings) {
                endings.append(e);
            }
            o["volta"] = QJsonObject { { "endings", endings }, { "measures", m.volta->measures }, { "text", m.volta->text } };
        } else {
            o["volta"] = QJsonValue(QJsonValue::Null);
        }
        QJsonArray jumps;
        for (const ChordChartJump& j : m.jumps) {
            jumps.append(QJsonObject { { "to", j.to }, { "until", j.until }, { "continue", j.cont }, { "text", j.text } });
        }
        o["jumps"] = jumps;
        QJsonArray markers;
        for (const ChordChartMarker& mk : m.markers) {
            markers.append(QJsonObject { { "label", mk.label }, { "text", mk.text } });
        }
        o["markers"] = markers;
        QJsonArray texts;
        for (const ChordChartText& t : m.texts) {
            texts.append(QJsonObject { { "k", t.k }, { "t", t.t }, { "lead", t.lead }, { "eid", t.eid } });
        }
        o["texts"] = texts;
        o["bar"] = optString(m.bar);
        QJsonArray chords;
        for (const ChordChartChord& c : m.chords) {
            chords.append(QJsonObject { { "root", c.root.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.root) },
                                        { "bass", c.bass.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.bass) },
                                        { "q", c.q }, { "hidden", c.hidden }, { "t", c.t } });
        }
        o["chords"] = chords;
        o["mmrTag"] = m.mmrTag;
        ms.append(o);
    }
    QJsonArray staves;
    for (int s : leadStaves) {
        staves.append(s);
    }
    return QJsonObject {
        { "file", file }, { "title", title }, { "subtitle", subtitle }, { "composer", composer }, { "lead", lead },
        { "leadStaves", staves }, { "leadStatus", optString(leadStatus) },
        { "key", key ? QJsonValue(*key) : QJsonValue(QJsonValue::Null) },
        { "chordChartHidden", QJsonArray::fromStringList(chordChartHidden) }, { "measures", ms }
    };
}

namespace {
//! extract.py's lead_staves: the staves (1-based) of the part called Lead (or Melody, Vocals); else of the part with
//! the most chord symbols
std::pair<QString, std::vector<int> > leadStavesOf(const MasterScore* ms)
{
    struct Range {
        QString name;
        std::vector<int> ids;
    };
    std::vector<Range> ranges;
    int sid = 1;
    for (const Part* p : ms->parts()) {
        Range r;
        // the file's Instrument/longName, else its trackName (the part name)
        const StaffNameList& names = p->instrument()->longNames();
        r.name = !names.empty() ? textOf(names.front().name()) : QString();
        if (r.name.isEmpty()) {
            r.name = p->partName().toQString().trimmed();
        }
        for (size_t k = 0; k < p->nstaves(); ++k) {
            r.ids.push_back(sid++);
        }
        ranges.push_back(r);
    }
    static const QRegularExpression leadRe("^(lead|lead sheet|melody|vocals?)$", QRegularExpression::CaseInsensitiveOption);
    for (const Range& r : ranges) {
        if (leadRe.match(r.name).hasMatch()) {
            return { r.name, r.ids };
        }
    }
    // chord symbols per staff (a fret diagram's chord counts too, as it did in the file)
    std::map<int, int> count;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        for (const Segment* s = m->first(); s; s = s->next()) {
            for (const EngravingItem* e : s->annotations()) {
                if (e->isHarmony() || (e->isFretDiagram() && toFretDiagram(e)->harmony())) {
                    ++count[int(e->staffIdx()) + 1];
                }
            }
        }
    }
    const Range* best = nullptr;
    int bestCount = -1;
    for (const Range& r : ranges) {
        int c = 0;
        for (int id : r.ids) {
            c += count.count(id) ? count.at(id) : 0;
        }
        if (c > bestCount) {   // max() keeps the first of equals
            bestCount = c;
            best = &r;
        }
    }
    if (!best) {
        return { QString(), {} };
    }
    return { best->name, best->ids };
}

//! The <name> the file holds for a chord (twrite's writeHarmonyInfo)
QString harmonyWrittenName(const HarmonyInfo* info)
{
    String name = info->textName();
    const bool hasNote = info->rootTpc() != Tpc::TPC_INVALID || info->bassTpc() != Tpc::TPC_INVALID;
    // the parser's leading "=" (a hidden minor marker) is written in front of the name, but only for a chord with a note
    if (hasNote && info->parsedChord() && info->parsedChord()->name().startsWith(u'=') && !name.startsWith(u'=')) {
        name = u"=" + name;
    }
    return name.toQString();
}
}

ChordChartData extractChordChart(const MasterScore* ms)
{
    ChordChartData data;
    if (!ms) {
        return data;
    }
    const auto [leadName, leadIds] = leadStavesOf(ms);
    data.lead = leadName;
    data.leadStaves = leadIds;
    auto isLead = [&](staff_idx_t staffIdx) {
        return std::find(leadIds.begin(), leadIds.end(), int(staffIdx) + 1) != leadIds.end();
    };
    const size_t nstaves = ms->nstaves();

    // The volta that starts in each measure (the file writes it in the voice where it starts)
    std::map<const Measure*, const Volta*> voltaAt;
    for (const auto& [tick, sp] : ms->spanner()) {
        if (!sp->isVolta() || sp->generated()) {
            continue;
        }
        const Measure* start = ms->tick2measure(sp->tick());
        if (start && !voltaAt.count(start)) {
            voltaAt[start] = toVolta(sp);
        }
    }

    // prototype: the bars are built from the first staff, merged with the structure (repeats, texts, barlines) of
    // every staff, in the order the file holds them: staff by staff, voice by voice, position by position
    struct RawChord {
        Fraction t;
        int staff = 0;
        ChordChartChord chord;
    };
    int idx = -1;
    for (const Measure* meas = ms->firstMeasure(); meas; meas = meas->nextMeasure()) {
        ++idx;
        ChordChartMeasure m;
        m.i = idx;
        const Fraction ts = meas->timesig();
        m.ts = QString("%1/%2").arg(ts.numerator()).arg(ts.denominator());
        const Fraction mlen = meas->ticks();
        m.len = fracStr(mlen);
        m.mmrTag = meas->mmRestCount() > 0;
        m.startRepeat = meas->repeatStart();
        m.endRepeat = meas->repeatEnd() ? std::max(0, meas->repeatCount()) : 0;
        if (auto v = voltaAt.find(meas); v != voltaAt.end()) {
            ChordChartVolta volta;
            volta.endings = v->second->endings();
            volta.text = textOf(v->second->beginText());
            if (volta.endings.empty()) {
                // prototype: endings parsed from the label when the list is empty
                static const QRegularExpression digits("\\d+");
                for (auto it = digits.globalMatch(volta.text); it.hasNext();) {
                    volta.endings.push_back(it.next().captured(0).toInt());
                }
            }
            // the file's <next><location><measures>: from the volta's first bar to the bar its end tick falls in
            const Measure* endIn = ms->tick2measure(v->second->tick2());
            volta.measures = (endIn ? endIn->measureIndex() : 0) - meas->measureIndex();
            m.volta = volta;
        }
        std::vector<RawChord> rawChords;
        for (staff_idx_t staff = 0; staff < nstaves; ++staff) {
            const bool lead = isLead(staff);
            // measure-level elements (jumps, markers) on this staff
            for (const EngravingItem* e : meas->el()) {
                if (e->generated() || e->staffIdx() != staff) {
                    continue;
                }
                if (e->isJump()) {
                    const Jump* j = toJump(e);
                    ChordChartJump jj { j->jumpTo().toQString(), j->playUntil().toQString(), j->continueAt().toQString(),
                                        textOf(j->xmlText()) };
                    const bool dup = std::any_of(m.jumps.begin(), m.jumps.end(), [&](const ChordChartJump& o) {
                        return o.to == jj.to && o.until == jj.until && o.cont == jj.cont && o.text == jj.text;
                    });
                    if (!dup) {
                        m.jumps.push_back(jj);
                    }
                } else if (e->isMarker()) {
                    const Marker* mk = toMarker(e);
                    ChordChartMarker mm { mk->label().toQString(), textOf(mk->xmlText()) };
                    const bool dup = std::any_of(m.markers.begin(), m.markers.end(), [&](const ChordChartMarker& o) {
                        return o.label == mm.label && o.text == mm.text;
                    });
                    if (!dup) {
                        m.markers.push_back(mm);
                    }
                }
            }
            for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
                const track_idx_t track = staff * VOICES + voice;
                for (const Segment* seg = meas->first(); seg; seg = seg->next()) {
                    const Fraction tick = seg->rtick();
                    // annotations of this voice, in the file's order
                    for (const EngravingItem* e : seg->annotations()) {
                        if (e->generated() || e->track() != track) {
                            continue;
                        }
                        if (e->isHarmony()) {
                            if (!lead) {
                                continue;
                            }
                            const Harmony* h = toHarmony(e);
                            if (h->chords().empty() || h->harmonyType() != HarmonyType::STANDARD) {
                                continue;   // not written / Roman numerals and Nashville numbers
                            }
                            const HarmonyInfo* info = h->chords().front();
                            RawChord rc;
                            rc.t = tick;
                            rc.staff = int(staff) + 1;
                            rc.chord.root = info->rootTpc() != Tpc::TPC_INVALID ? tpcName(info->rootTpc()) : QString();
                            rc.chord.bass = info->bassTpc() != Tpc::TPC_INVALID ? tpcName(info->bassTpc()) : QString();
                            rc.chord.q = harmonyWrittenName(info);
                            rc.chord.hidden = !h->visible();
                            rc.chord.t = fracStr(tick);
                            rawChords.push_back(rc);
                        } else if (e->isRehearsalMark()) {
                            const QString tx = textOf(toTextBase(e)->xmlText());
                            if (!m.rm || m.rm->isEmpty()) {
                                m.rm = tx;
                            }
                        } else if (e->isSystemText() || e->isStaffText() || e->isTempoText()) {
                            const QString kind = e->isSystemText() ? "SystemText" : e->isStaffText() ? "StaffText" : "Tempo";
                            const QString tx = textOf(toTextBase(e)->xmlText());
                            // prototype: system texts and tempos from any staff; staff texts only from the lead staves
                            if ((!tx.isEmpty() && kind != "StaffText") || lead) {
                                const bool seen = std::any_of(m.texts.begin(), m.texts.end(),
                                                              [&](const ChordChartText& t) { return t.t == tx; });
                                if (!tx.isEmpty() && !seen) {
                                    m.texts.push_back(ChordChartText { kind, tx, lead, eidOf(e) });
                                }
                            }
                        }
                    }
                    // barlines the user set (generated ones aren't in the file)
                    const EngravingItem* el = seg->element(track);
                    if (el && el->isBarLine() && !el->generated()) {
                        const BarLineType type = toBarLine(el)->barLineType();
                        if (type != BarLineType::NORMAL && (!m.bar || m.bar->isEmpty())) {
                            m.bar = QString::fromLatin1(TConv::toXml(type).ascii());
                        }
                    }
                    // the first staff's key signature
                    if (staff == 0 && !data.key && el && el->isKeySig() && !el->generated()) {
                        const KeySig* ks = toKeySig(el);
                        if (!ks->isAtonal()) {
                            data.key = int(ks->concertKey());
                        }
                    }
                }
            }
        }
        // prototype: keep the lead chords; when chords sit on several lead staves at the same position, keep one
        std::stable_sort(rawChords.begin(), rawChords.end(), [](const RawChord& a, const RawChord& b) {
            return a.t < b.t || (a.t == b.t && a.staff < b.staff);
        });
        for (const RawChord& rc : rawChords) {
            if (m.chords.empty() || m.chords.back().t != rc.chord.t) {
                m.chords.push_back(rc.chord);
            }
        }
        data.measures.push_back(m);
    }

    // the StarScore data: the Lead Sheet section's status, overridden by its parts' own tags (the prototype's reading)
    const QJsonObject ssData = QJsonDocument::fromJson(ms->metaTag(u"starscore").toQString().toUtf8()).object();
    for (const QJsonValue& v : ssData.value("sections").toArray()) {
        const QJsonObject s = v.toObject();
        if (s.value("template").toString() == "lead-sheet" || s.value("id").toString() == "lead-sheet") {
            data.leadStatus = s.value("status").toString();
            const QJsonObject ps = ssData.value("partStatus").toObject();
            for (const QJsonValue& pid : s.value("parts").toArray()) {
                if (ps.contains(pid.toString())) {
                    data.leadStatus = ps.value(pid.toString()).toString();
                }
            }
        }
    }
    for (const QJsonValue& v : ssData.value("chordChartHidden").toArray()) {
        data.chordChartHidden << v.toString();
    }
    for (const QJsonValue& v : QJsonDocument::fromJson(ms->metaTag(u"starscoreChordChartHidden").toQString().toUtf8()).array()) {
        data.chordChartHidden << v.toString();
    }

    // the title frame: the last text of each style wins, as in the file
    std::map<TextStyleType, QString> frameTexts;
    for (const MeasureBase* mb = ms->first(); mb; mb = mb->next()) {
        if (!mb->isVBox()) {
            continue;
        }
        for (const EngravingItem* e : mb->el()) {
            if (e->isText()) {
                frameTexts[toText(e)->textStyleType()] = textOf(toText(e)->xmlText());
            }
        }
        break;
    }
    data.title = frameTexts.count(TextStyleType::TITLE) ? frameTexts[TextStyleType::TITLE] : QString();
    if (data.title.isEmpty()) {
        data.title = ms->metaTag(u"workTitle").toQString();
    }
    data.subtitle = frameTexts.count(TextStyleType::SUBTITLE) ? frameTexts[TextStyleType::SUBTITLE] : QString();
    data.composer = frameTexts.count(TextStyleType::COMPOSER) ? frameTexts[TextStyleType::COMPOSER] : QString();
    return data;
}

bool chordChartPossible(const ChordChartData& data, QString* why)
{
    static const QRegularExpression leadRe("^(lead|lead sheet)$", QRegularExpression::CaseInsensitiveOption);
    if (!leadRe.match(data.lead).hasMatch()) {
        if (why) {
            *why = muse::qtrc("starscore", "no Lead part (the chords would come from %1)").arg(data.lead.isEmpty() ? "?" : data.lead);
        }
        return false;
    }
    int chords = 0;
    for (const ChordChartMeasure& m : data.measures) {
        chords += int(m.chords.size());
    }
    if (chords == 0) {
        if (why) {
            *why = muse::qtrc("starscore", "no chord symbols on the lead sheet");
        }
        return false;
    }
    return true;
}

QString chordChartTitleFromFileName(const QString& path)
{
    QString title = QFileInfo(path).completeBaseName();
    static const QRegularExpression coded("^[A-Z]{4} - ");
    title.remove(coded);
    return title;
}

// ============================================================================================ 2. analysis (chart.py)
namespace {
//! A chord as the analysis compares it: "root|quality|bass", or "NC" for no chord
QString chordKey(const ChordChartChord& c)
{
    if (c.root.isEmpty()) {
        return "NC";
    }
    return c.root + "|" + c.q + "|" + c.bass;
}

//! [(position in the bar, chord key)]
using ChordList = std::vector<std::pair<Fraction, QString> >;

bool sameChords(const ChordList& a, const ChordList& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first || a[i].second != b[i].second) {
            return false;
        }
    }
    return true;
}

struct Bar {
    int i = 0;
    QString ts;
    Fraction len;
    Fraction tsf;                       // the time signature as a fraction (reduced, as the prototype's Fraction was)
    QString rm;                         // rehearsal mark, "" for none
    bool start = false;
    int end = 0;
    std::optional<ChordChartVolta> volta;
    std::vector<ChordChartText> texts;
    std::vector<ChordChartChord> raw;   // sorted by position
    std::optional<std::vector<int> > endings;   // the volta this bar is under
};

//! What a written bar is for the compression: time signature, length and chords
struct BarKey {
    QString ts;
    QString len;
    ChordList ch;
    bool operator==(const BarKey& o) const { return ts == o.ts && len == o.len && sameChords(ch, o.ch); }
    bool operator!=(const BarKey& o) const { return !(*this == o); }
    //! for dictionaries keyed by a list of bars
    QString sig() const
    {
        QString s = ts + "\x1f" + len;
        for (const auto& [pos, k] : ch) {
            s += "\x1e" + fracStr(pos) + "\x1d" + k;
        }
        return s;
    }
};
using Keys = std::vector<BarKey>;

bool sameKeys(const Keys& a, size_t ai, const Keys& b, size_t bi, size_t n)
{
    for (size_t k = 0; k < n; ++k) {
        if (a[ai + k] != b[bi + k]) {
            return false;
        }
    }
    return true;
}

Keys slice(const Keys& k, size_t from, size_t to)
{
    return Keys(k.begin() + from, k.begin() + to);
}

QString keysSig(const Keys& keys)
{
    QString s;
    for (const BarKey& k : keys) {
        s += k.sig() + "\x1c";
    }
    return s;
}

//! compress()'s result: one bar; the bars played n times; a body with one ending per pass; a run of empty bars
struct Item {
    enum Kind { BarItem, Rep, End, Rest };
    Kind kind = BarItem;
    Keys keys;                      // BarItem: the bar; Rep: the bars; End: the body; Rest: the (first) bar
    int n = 0;                      // Rep: times; Rest: bars
    std::vector<Keys> ends;         // End: the endings
};

struct FilledBar {
    const Bar* b = nullptr;
    ChordList ch;
};

struct Section {
    QString label;
    int start = 0;
    std::vector<FilledBar> bars;
    QStringList texts;
    Keys keys;
};

struct Group {
    QStringList labels;
    Keys keys;
    int times = 1;
    QStringList texts;
    std::optional<QString> same;    // an earlier group with the same chords: its first label
    std::vector<Item> items;
};

struct Analysis {
    std::vector<Group> groups;
    QString tempo;
    std::vector<Bar> bars;
    std::vector<int> order;
};

std::vector<Bar> buildBars(const ChordChartData& song)
{
    std::vector<Bar> bars;
    for (const ChordChartMeasure& m : song.measures) {
        Bar b;
        b.i = m.i;
        b.ts = m.ts;
        b.len = parseFrac(m.len);
        b.tsf = parseFrac(m.ts);
        b.rm = m.rm ? m.rm->trimmed() : QString();
        b.start = m.startRepeat;
        b.end = m.endRepeat;
        b.volta = m.volta;
        b.texts = m.texts;
        b.raw = m.chords;
        std::stable_sort(b.raw.begin(), b.raw.end(), [](const ChordChartChord& x, const ChordChartChord& y) {
            return parseFrac(x.t) < parseFrac(y.t);
        });
        bars.push_back(b);
    }
    // voltas: (start bar, measures) -> endings on each bar
    for (size_t k = 0; k < bars.size(); ++k) {
        if (bars[k].volta) {
            const size_t to = std::min(bars.size(), k + size_t(std::max(1, bars[k].volta->measures)));
            for (size_t j = k; j < to; ++j) {
                bars[j].endings = bars[k].volta->endings.empty() ? std::vector<int> { 1 } : bars[k].volta->endings;
            }
        }
    }
    return bars;
}

//! Written repeats and 1st/2nd endings played out: the bars in the order they are played
std::vector<int> playOrder(const std::vector<Bar>& bars)
{
    std::vector<int> order;
    const int n = int(bars.size());
    int i = 0;
    int repStart = 0;
    std::map<int, int> passes;   // repeat start bar -> passes done
    auto passesOf = [&](int start) { auto it = passes.find(start); return it == passes.end() ? 1 : it->second; };
    int guard = 0;
    while (i < n && guard < 20000) {
        ++guard;
        const Bar& b = bars[i];
        if (b.start) {
            repStart = i;
        }
        const int curPass = passesOf(repStart);
        if (b.endings && std::find(b.endings->begin(), b.endings->end(), curPass) == b.endings->end()) {
            // skip this ending; jump past it
            ++i;
            continue;
        }
        order.push_back(i);
        if (b.end) {
            const int total = b.end;
            const int done = passesOf(repStart);
            if (done < total) {
                passes[repStart] = done + 1;
                i = repStart;
                continue;
            }
            passes[repStart] = 1;
            // a new repeat (if any) starts after this bar, unless a { says otherwise
            repStart = i + 1;
        } else if (b.endings && i + 1 < n && !bars[i + 1].endings && passesOf(repStart) > 1) {
            // last ending done
            passes[repStart] = 1;
            repStart = i + 1;
        }
        ++i;
    }
    return order;
}

QString soundingBefore(const std::vector<FilledBar>& out)
{
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        if (!it->ch.empty()) {
            return it->ch.back().second;
        }
    }
    return QString();
}

//! Each played bar with the chord sounding on beat 1 filled in, hidden repeats dropped
std::vector<FilledBar> fillChords(const std::vector<const Bar*>& seq)
{
    std::vector<FilledBar> out;
    QString sounding;   // null = none yet
    for (const Bar* b : seq) {
        ChordList ch;
        for (const ChordChartChord& c : b->raw) {
            const QString k = chordKey(c);
            const Fraction pos = parseFrac(c.t);
            if (pos >= b->len) {
                continue;
            }
            if (!ch.empty() && ch.back().first == pos) {
                continue;
            }
            if (k == sounding) {
                continue;   // the same chord again (often hidden, for playback)
            }
            ch.push_back({ pos, k });
            sounding = k;
        }
        // prototype: the chord still sounding is written at the start of the bar, unless the bar's first chord
        // comes within its first beat (a push: the new chord stands for the bar). The "beat" is 1/denominator of
        // the time signature as a reduced fraction, so in 4/4 (= 1) it is the whole bar.
        const Fraction beat(1, b->tsf.reduced().denominator());
        if (ch.empty() || ch.front().first >= beat) {
            const QString before = soundingBefore(out);
            if (!before.isNull()) {
                ch.insert(ch.begin(), { Fraction(0, 1), before });
            }
        } else if (!ch.front().first.isZero()) {
            ch.front().first = Fraction(0, 1);
        }
        out.push_back({ b, ch });
        if (!ch.empty()) {
            sounding = ch.back().second;
        }
    }
    return out;
}

//! The played bars split at the lead sheet's rehearsal marks (a repeat back to a section's own start stays in that
//! section); texts unticked "Visible on chord chart" are left out
std::vector<Section> splitSections(const std::vector<FilledBar>& filled, const std::set<QString>& hidden)
{
    std::vector<Section> sections;
    Section* cur = nullptr;
    for (const FilledBar& fb : filled) {
        const Bar& b = *fb.b;
        if (!b.rm.isEmpty() && (!cur || cur->start != b.i)) {
            sections.push_back(Section { b.rm, b.i, {}, {}, {} });
            cur = &sections.back();
        } else if (!cur) {
            sections.push_back(Section { QString(), b.i, {}, {}, {} });
            cur = &sections.back();
        }
        cur->bars.push_back(fb);
        for (const ChordChartText& t : b.texts) {
            if (!cur->texts.contains(t.t) && t.k != "Tempo" && !hidden.count(t.eid)) {
                cur->texts << t.t;
            }
        }
    }
    return sections;
}

//! chart.py's compress: a section written with the fewest bars: runs of the same bars become repeats (xN), passes
//! that differ only at the end become 1st/2nd endings. (Used for the iReal charts; the PDF has its own, below.)
std::vector<Item> compress(const Keys& keys)
{
    const int n = int(keys.size());
    const double INF = 1e9;
    std::vector<double> best(n + 1, INF);
    struct Choice {
        int kind = 0;   // 0 bar, 1 rep, 2 end
        int p = 1, k = 1, e = 0;
    };
    std::vector<Choice> choice(n + 1);
    best[n] = 0;
    for (int i = n - 1; i >= 0; --i) {
        best[i] = 1 + best[i + 1];
        choice[i] = Choice { 0, 1, 1, 0 };
        for (int p = 1; p <= std::min(16, n - i); ++p) {
            // plain repeat of p bars, k times
            int k = 1;
            while (i + (k + 1) * p <= n && sameKeys(keys, i + k * p, keys, i, p)) {
                ++k;
            }
            for (int kk = 2; kk <= k; ++kk) {
                const double cost = p + 1.25 + best[i + kk * p];
                if (cost < best[i]) {
                    best[i] = cost;
                    choice[i] = Choice { 1, p, kk, 0 };
                }
            }
            // passes that differ only in their last e bars (endings)
            for (int e = 1; e <= 2; ++e) {
                const int body = p - e;
                if (body < 2) {
                    continue;
                }
                k = 1;
                while (i + (k + 1) * p <= n && sameKeys(keys, i + k * p, keys, i, body)) {
                    ++k;
                }
                if (k < 2) {
                    continue;
                }
                bool allSame = true;
                for (int j = 1; j < k && allSame; ++j) {
                    allSame = sameKeys(keys, i + j * p + body, keys, i + body, e);
                }
                if (allSame) {
                    continue;   // a plain repeat already
                }
                for (int kk = 2; kk <= std::min(k, 4); ++kk) {
                    const double cost = body + e * kk + 1.0 + best[i + kk * p];
                    if (cost < best[i] - 0.01) {
                        best[i] = cost;
                        choice[i] = Choice { 2, p, kk, e };
                    }
                }
            }
        }
    }
    std::vector<Item> items;
    int i = 0;
    while (i < n) {
        const Choice c = choice[i];
        if (c.kind == 0) {
            items.push_back(Item { Item::BarItem, { keys[i] }, 0, {} });
            i += 1;
        } else if (c.kind == 1) {
            items.push_back(Item { Item::Rep, slice(keys, i, i + c.p), c.k, {} });
            i += c.p * c.k;
        } else {
            const int body = c.p - c.e;
            Item it { Item::End, slice(keys, i, i + body), 0, {} };
            for (int j = 0; j < c.k; ++j) {
                it.ends.push_back(slice(keys, i + j * c.p + body, i + (j + 1) * c.p));
            }
            items.push_back(it);
            i += c.p * c.k;
        }
    }
    return items;
}

int displayedBars(const std::vector<Item>& items)
{
    int t = 0;
    for (const Item& it : items) {
        if (it.kind == Item::BarItem) {
            t += 1;
        } else if (it.kind == Item::Rep) {
            t += int(it.keys.size());
        } else {
            t += int(it.keys.size());
            for (const Keys& e : it.ends) {
                t += int(e.size());
            }
        }
    }
    return t;
}

Analysis analyse(const ChordChartData& song)
{
    Analysis a;
    a.bars = buildBars(song);
    a.order = playOrder(a.bars);
    std::vector<const Bar*> seq;
    for (int i : a.order) {
        seq.push_back(&a.bars[i]);
    }
    std::vector<FilledBar> filled = fillChords(seq);
    // a pickup bar (shorter than its time signature) with no chords isn't on the chart
    while (!filled.empty() && filled.front().b->len < filled.front().b->tsf && filled.front().b->raw.empty()) {
        filled.erase(filled.begin());
    }
    std::set<QString> hidden(song.chordChartHidden.begin(), song.chordChartHidden.end());
    std::vector<Section> sections;
    for (Section& s : splitSections(filled, hidden)) {
        const bool anyChords = std::any_of(s.bars.begin(), s.bars.end(), [](const FilledBar& fb) { return !fb.ch.empty(); });
        if (!s.label.isEmpty() || anyChords) {
            sections.push_back(std::move(s));
        }
    }
    // a pickup with chords before the first rehearsal mark is its own section
    if (!sections.empty() && sections.front().label.isEmpty()
        && std::all_of(sections.front().bars.begin(), sections.front().bars.end(),
                       [](const FilledBar& fb) { return fb.b->len < fb.b->tsf; })) {
        sections.front().label = "Pickup";
    }
    // group consecutive sections with the same chords; refer to earlier identical sections
    for (Section& s : sections) {
        for (const FilledBar& fb : s.bars) {
            s.keys.push_back(BarKey { fb.b->ts, fracStr(fb.b->len), fb.ch });
        }
        if (!a.groups.empty() && a.groups.back().keys == s.keys && !s.label.isEmpty() && a.groups.back().labels.last() != s.label) {
            Group& g = a.groups.back();
            g.labels << s.label;
            g.times += 1;
            for (const QString& t : s.texts) {
                if (!g.texts.contains(t)) {
                    g.texts << t;
                }
            }
            continue;
        }
        a.groups.push_back(Group { { s.label }, s.keys, 1, s.texts, std::nullopt, {} });
    }
    std::map<QString, QString> seen;
    for (Group& g : a.groups) {
        const QString sig = keysSig(g.keys);
        auto it = seen.find(sig);
        if (it != seen.end()) {
            g.same = it->second;
        } else {
            seen[sig] = g.labels.first();
        }
        g.items = compress(g.keys);
    }
    for (size_t k = 0; k < std::min<size_t>(2, a.bars.size()); ++k) {
        for (const ChordChartText& t : a.bars[k].texts) {
            if (t.k == "Tempo") {
                a.tempo = t.t;
            }
        }
    }
    return a;
}

//! "F1 (Solo #1)" -> "F1"
QString shortLabel(const QString& l)
{
    QString s = l;
    if (s.endsWith(')')) {
        const int open = s.indexOf('(');
        if (open >= 0) {
            s = s.left(open);
        }
    }
    return s.trimmed();
}

//! The text in a label's final parentheses ("F1 (Solo #1)" -> "Solo #1"), none without them
std::optional<QString> labelNote(const QString& l)
{
    if (!l.endsWith(')')) {
        return std::nullopt;
    }
    const int open = l.indexOf('(');
    if (open < 0) {
        return std::nullopt;
    }
    return l.mid(open + 1, l.size() - open - 2);
}
}

// ============================================================================================ 3. the PDF page (pdfchart.py)
namespace {
//! prototype: the look of pdfchart.py — rows of 4 bars (2 in sections with 3+ chords to a bar); a bar's width
//! follows its length; each section starts a new row, its rehearsal mark in the left margin; time signatures at the
//! start and wherever they change; repeats only where they save at least 4 bars; 1st/2nd endings only for 8- and
//! 16-bar phrases; bars with no chords as one multi-bar rest; chords as in iReal Pro.
const char* PDF_CSS = R"CSS(
@font-face { font-family: Modernoir; src: url('%(modernoir)s'); }
@font-face { font-family: SSJost; src: url('%(jost)s'); }
@font-face { font-family: BravuraText; src: url('%(bravura)s'); }
:root { --R: 30pt; --ink: #141414; --muted: #6b6b6b; --rule: #1d1d1d; --inset: 11pt; }
html, body { margin: 0; padding: 0; background: #fff; }
#page { width: 360pt; min-height: 780pt; box-sizing: border-box; padding: 22pt 4pt 28pt 6pt; color: var(--ink);
        font-family: SSJost, sans-serif; }
header { text-align: center; margin: 0 0 16pt; }
h1 { font-family: Modernoir, sans-serif; font-weight: normal; font-size: 34pt; letter-spacing: 0.05em; margin: 0; line-height: 1.05; }
.sub { font-family: Modernoir, sans-serif; font-size: 16pt; color: #333; margin-top: 5pt; letter-spacing: 0.03em; }
.sub .note { font-family: BravuraText; font-size: 16pt; position: relative; top: 0.28em; }
.form { font-family: Modernoir, sans-serif; font-size: 14pt; color: #333; margin: 8pt 14pt 0; line-height: 1.45; letter-spacing: 0.03em; }
.form .sep { color: #aaa; }
section { margin-top: 14pt; }
.head { display: flex; align-items: center; gap: 8pt; margin: 0 0 4pt 4pt; }
.notes { font-family: Modernoir, sans-serif; font-size: 14pt; color: #444; letter-spacing: 0.02em; line-height: 1.15; }
.row { display: grid; align-items: stretch; margin-top: 15pt; }
section > .row:first-child, .head + .row { margin-top: 8pt; }
.gut { position: relative; }
.lab { font-family: Modernoir, sans-serif; font-size: 18pt; line-height: 1; padding: 3pt 4pt 2pt;
       border: 1.3pt solid var(--ink); min-width: 11pt; text-align: center; background: #fff; white-space: nowrap; }
.gut .lab { position: absolute; left: 2pt; top: 50%%; transform: translateY(-50%%); }
.mts { position: absolute; right: 3pt; top: 50%%; transform: translateY(-50%%); display: flex; flex-direction: column;
       font-size: 15pt; line-height: 0.86; font-weight: 700; text-align: center; }
.gut .lab + .mts { right: 1pt; }
.cnt { font-size: 13pt; font-weight: 700; display: flex; align-items: center; padding-left: 4pt; }
.pad { display: flex; align-items: center; }
.bar { position: relative; height: calc(var(--R) * 1.5); border-left: 1pt solid var(--rule); min-width: 0; }
.bar.tall { height: calc(var(--R) * 1.95); }
.bar.closing { border-right: 1pt solid var(--rule); }
.row.last .bar.closing:not(.re) { border-right: 3.2pt double var(--rule); }
/* repeat barlines: thick line, thin line, two dots */
.bar.rs { border-left: 3pt solid var(--rule); }
.bar.rs::before { content: ''; position: absolute; left: 1.6pt; top: 0; bottom: 0; border-left: 0.9pt solid var(--rule); }
.bar.re { border-right: 3pt solid var(--rule) !important; }
.bar.re::after { content: ''; position: absolute; right: 1.6pt; top: 0; bottom: 0; border-right: 0.9pt solid var(--rule); }
.dot { position: absolute; width: 3.2pt; height: 3.2pt; border-radius: 50%%; background: var(--rule); }
.dot.l { left: 4pt; } .dot.r { right: 4pt; }
.dot.d1 { top: calc(50%% - 6pt); } .dot.d2 { top: calc(50%% + 2.8pt); }
/* endings: a bracket in the space above the bars (drawn by the script) */
.bar.en .enn { position: absolute; left: 4pt; top: -17pt; font-size: 11pt; font-weight: 700; }
/* chords */
.slot { position: absolute; left: calc(var(--inset) + var(--at) * (100%% - var(--inset) - 6pt)); top: calc(var(--R) * 0.17); }
.bar.hasts .slot.s0 { left: calc(var(--inset) + 10pt); }
.c { display: inline-block; position: relative; white-space: nowrap; font-size: calc(var(--R) * var(--fs, 1)); line-height: 1;
     transform-origin: 0 0; transform: scaleX(var(--sx, 1)); font-kerning: none; }
.rw { position: relative; display: inline-block; }
.r { font-weight: 500; }
.ac { position: absolute; left: 100%%; top: -0.06em; font-size: 0.78em; line-height: 1; padding-left: 0.07em; }
.acc { font-family: BravuraText; -webkit-text-stroke: 0.045em currentColor; }
.q { font-size: 0.5em; margin-left: 0.05em; letter-spacing: 0.01em; }
.c.hasacc .q { display: inline-block; min-width: 0.95em; }
.q .acc { font-size: 1.1em; position: relative; top: -0.05em; }
.bs { position: absolute; left: 0.95em; top: 1.95em; font-size: 0.46em; }
.bs .sl { margin-right: 0.04em; }
.bs .acc { font-size: 0.9em; position: relative; top: -0.3em; margin-left: 0.03em; }
.c.nc { font-size: calc(var(--R) * 0.62); font-weight: 500; top: 0.35em; }
.sim { position: absolute; left: 50%%; top: 50%%; transform: translate(-50%%, -50%%); font-family: BravuraText; font-size: 30pt; color: #333; }
.ts { position: absolute; left: 4pt; top: 50%%; transform: translateY(-50%%); display: flex; flex-direction: column;
      font-size: 15pt; line-height: 0.86; font-weight: 700; text-align: center; }
.bar.rs .ts { left: 10pt; }
.bar.rs.hasts .slot.s0 { left: calc(var(--inset) + 16pt); }
/* multi-bar rest, centred between the barlines */
.bar.rest .mr { position: absolute; left: 16%%; right: 16%%; top: 50%%; transform: translateY(-50%%); }
.mrb { display: block; height: 7pt; background: var(--rule); border-left: 1pt solid var(--rule); border-right: 1pt solid var(--rule); }
.mrn { display: block; text-align: center; font-size: 15pt; font-weight: 700; line-height: 1; margin-bottom: 4pt; }
)CSS";

const char* PDF_JS = R"JS(
<script>
(function () {
  const PX = 96 / 72;
  function chordsOf(bar) { return Array.from(bar.querySelectorAll('.c')); }
  function crowded(bar) {
    const cs = chordsOf(bar);
    if (!cs.length) return false;
    const br = bar.getBoundingClientRect();
    const limit = br.right - (bar.classList.contains('re') ? 13 * PX : 4 * PX);
    for (let i = 0; i < cs.length; i++) {
      const r = cs[i].getBoundingClientRect();
      if (r.right > limit) return true;
      if (i + 1 < cs.length && r.right + 7 * PX > cs[i + 1].getBoundingClientRect().left) return true;
    }
    return false;
  }
  // width a bar needs for its chords side by side at full size
  function need(bar) {
    const cs = chordsOf(bar);
    if (!cs.length) return 0;
    let w = 0;
    cs.forEach(c => { w += c.getBoundingClientRect().width; });
    const inset = parseFloat(getComputedStyle(bar.querySelector('.slot')).left) || 15;
    return w + (cs.length - 1) * 9 * PX + inset + (bar.classList.contains('re') ? 14 : 6) * PX;
  }
  document.querySelectorAll('.row').forEach(row => {
    const cells = Array.from(row.children).slice(1, -1);
    const bars = cells.filter(c => c.classList.contains('bar'));
    if (!bars.some(crowded)) return;
    // a crowded bar takes width from bars in the row that have room
    const widths = cells.map(c => c.getBoundingClientRect().width);
    const total = widths.reduce((a, b) => a + b, 0);
    const minw = cells.map((c, i) => c.classList.contains('bar') ? Math.max(need(c), 0.42 * widths[i] / (parseFloat(c.dataset.w) || 1) * (parseFloat(c.dataset.w) || 1)) : 0);
    const want = cells.map((c, i) => c.classList.contains('bar') ? Math.max(widths[i], minw[i]) : widths[i]);
    let over = want.reduce((a, b) => a + b, 0) - total;
    const give = cells.map((c, i) => Math.max(0, want[i] - minw[i]) * (want[i] <= widths[i] ? 1 : 0));
    const room = give.reduce((a, b) => a + b, 0);
    const out = want.map((w, i) => room > 0 && over > 0 ? w - give[i] * Math.min(1, over / room) : w);
    let sum = out.reduce((a, b) => a + b, 0);
    const scale = sum > total ? total / sum : 1;
    const parts = row.style.gridTemplateColumns.split(' ');
    const cols = [parts[0]].concat(out.map(w => (w * scale).toFixed(2) + 'px')).concat([parts[parts.length - 1]]);
    row.style.gridTemplateColumns = cols.join(' ');
  });
  document.querySelectorAll('.bar').forEach(b => {
    // still too tight: chords spaced evenly, then narrower (as iReal does), then smaller
    if (crowded(b)) {
      const slots = Array.from(b.querySelectorAll('.slot'));
      slots.forEach((s, i) => s.style.setProperty('--at', (i / slots.length).toFixed(3)));
    }
    let sx = 1;
    while (crowded(b) && sx > 0.7) { sx -= 0.05; b.style.setProperty('--sx', sx); }
    let fs = 1;
    while (crowded(b) && fs > 0.6) { fs -= 0.05; b.style.setProperty('--fs', fs); }
  });
  // ending brackets: a line in the space above the bars, a hook at the start and end
  document.querySelectorAll('.bar.en').forEach(b => {
    const l = document.createElement('i');
    l.className = 'enl';
    l.style.cssText = 'position:absolute;left:0;right:0;top:-6pt;border-top:1.2pt solid #1d1d1d;' +
      (b.classList.contains('enstart') ? 'border-left:1.2pt solid #1d1d1d;height:9pt;' : '') +
      (b.classList.contains('enend') || b.classList.contains('re') ? 'border-right:1.2pt solid #1d1d1d;height:9pt;' : '');
    b.appendChild(l);
  });
  const page = document.getElementById('page');
  document.body.dataset.cols = 4;
  document.body.dataset.scale = 1;
  document.body.dataset.height = Math.ceil(page.getBoundingClientRect().height * 0.75);
})();
</script>
)JS";

//! The version footer under the last row: part of the page, so the page height (data-height) includes it
const char* PDF_FOOTER_CSS = "\n.ver { font-family: Modernoir, sans-serif; font-size: 9pt; color: #9a9a9a; text-align: center; "
                             "margin: 22pt 10pt 0; letter-spacing: 0.03em; }\n";

//! Bravura Text glyphs: accidentalFlat and accidentalSharp (prototype: heavier than the chord-symbol flat)
const QString FLAT = QString::fromUtf8("<span class=\"acc\">\xee\x89\xa0</span>");
const QString SHARP = QString::fromUtf8("<span class=\"acc\">\xee\x89\xa2</span>");

QString accHtml(const QString& text)
{
    QString out;
    for (const QChar ch : text) {
        out += ch == 'b' ? FLAT : ch == '#' ? SHARP : esc(QString(ch));
    }
    return out;
}

//! MuseScore's typed chord text drawn as on the lead sheet (StarScore Jost): ^ triangle, 0 half-diminished, o
//! diminished circle; flats and sharps in the alterations
QString qualityHtml(const QString& q)
{
    QString out;
    for (int i = 0; i < q.size(); ++i) {
        const QChar c = q[i];
        if (c == '^') {
            out += QString::fromUtf8("\xee\x80\x80");          // U+E000
        } else if (c == '0' && i == 0) {
            out += QString::fromUtf8("\xc3\xb8");              // ø
        } else if (c == 'o' && i == 0) {
            out += QString::fromUtf8("\xee\x81\x80");          // U+E040
        } else if ((c == 'b' || c == '#') && i + 1 < q.size() && q[i + 1].isDigit()) {
            out += c == 'b' ? FLAT : SHARP;
        } else {
            out += esc(QString(c));
        }
    }
    return out;
}

//! iReal Pro's layout: the letter large; its flat or sharp small and high, the quality small on the baseline, a slash
//! chord's bass note below
QString chordHtml(const QString& key)
{
    if (key == "NC") {
        return "<span class=\"c nc\">N.C.</span>";
    }
    const QStringList parts = key.split('|');
    const QString root = parts.value(0), q = parts.value(1), bass = parts.value(2);
    const QString acc = root.mid(1);
    QString cls = "c" + QString(acc.isEmpty() ? "" : " hasacc") + QString(bass.isEmpty() ? "" : " hasbass");
    QString s = QString("<span class=\"%1\"><span class=\"rw\"><span class=\"r\">%2</span>").arg(cls, esc(root.left(1)));
    if (!acc.isEmpty()) {
        s += QString("<span class=\"ac\">%1</span>").arg(accHtml(acc));
    }
    s += QString("</span><span class=\"q\">%1</span>").arg(q.isEmpty() ? QString() : qualityHtml(q));
    if (!bass.isEmpty()) {
        s += QString("<span class=\"bs\"><span class=\"sl\">%1</span>%2%3</span>").arg(QString::fromUtf8("\xe2\x88\x95"),
                                                                                      esc(bass.left(1)), accHtml(bass.mid(1)));
    }
    return s + "</span>";
}

QString tsHtml(const QString& ts, const QString& cls = "ts")
{
    const QStringList nd = ts.split('/');
    return QString("<span class=\"%1\"><b>%2</b><b>%3</b></span>").arg(cls, nd.value(0), nd.value(1));
}

//! pdfchart.py's compress_readable: items are bars, repeats ("rep"), endings ("end") and multi-bar rests ("rest").
//! prototype: 2-bar figures 3+ times, 4- and 8-bar phrases twice or more (a single chord held: a line of 4,
//! repeated); endings only for 8- and 16-bar phrases.
std::vector<Item> compressReadable(const Keys& keys)
{
    const int n = int(keys.size());
    const double INF = 1e9;
    std::vector<double> best(n + 1, INF);
    struct Choice {
        int kind = 0;   // 0 bar, 1 rest, 2 rep, 3 end
        int r = 0, p = 0, k = 0, e = 0;
    };
    std::vector<Choice> choice(n + 1);
    best[n] = 0;
    std::vector<bool> empty(n);
    for (int i = 0; i < n; ++i) {
        empty[i] = keys[i].ch.empty();
    }
    for (int i = n - 1; i >= 0; --i) {
        best[i] = 1 + best[i + 1];
        choice[i] = Choice { 0, 0, 0, 0, 0 };
        if (empty[i]) {
            int r = 0;
            while (i + r < n && empty[i + r] && keys[i + r].ts == keys[i].ts && keys[i + r].len == keys[i].len) {
                ++r;
            }
            if (r >= 2 && 1.2 + best[i + r] < best[i]) {
                best[i] = 1.2 + best[i + r];
                choice[i] = Choice { 1, r, 0, 0, 0 };
            }
        }
        for (const int p : { 2, 4, 8 }) {
            if (i + 2 * p > n || i % std::min(p, 4)) {
                continue;
            }
            if (p == 2 && keys[i] == keys[i + 1]) {
                continue;   // one chord held: a full line of 4 instead
            }
            int k = 1;
            while (i + (k + 1) * p <= n && sameKeys(keys, i + k * p, keys, i, p)) {
                ++k;
            }
            for (int kk = 2; kk <= k; ++kk) {
                if (p * (kk - 1) < 4) {
                    continue;
                }
                const double cost = p + 0.6 + best[i + kk * p];
                if (cost < best[i]) {
                    best[i] = cost;
                    choice[i] = Choice { 2, 0, p, kk, 0 };
                }
            }
        }
        for (const int p : { 8, 16 }) {
            if (i + 2 * p > n || i % 4) {
                continue;
            }
            for (const int e : { 1, 2 }) {
                const int body = p - e;
                if (!sameKeys(keys, i + p, keys, i, body) || sameKeys(keys, i + body, keys, i + p + body, e)) {
                    continue;
                }
                const double cost = body + 2 * e + 0.8 + best[i + 2 * p];
                if (cost < best[i]) {
                    best[i] = cost;
                    choice[i] = Choice { 3, 0, p, 0, e };
                }
            }
        }
    }
    std::vector<Item> items;
    int i = 0;
    while (i < n) {
        const Choice c = choice[i];
        if (c.kind == 0) {
            items.push_back(Item { Item::BarItem, { keys[i] }, 0, {} });
            i += 1;
        } else if (c.kind == 1) {
            items.push_back(Item { Item::Rest, { keys[i] }, c.r, {} });
            i += c.r;
        } else if (c.kind == 2) {
            items.push_back(Item { Item::Rep, slice(keys, i, i + c.p), c.k, {} });
            i += c.p * c.k;
        } else {
            const int body = c.p - c.e;
            items.push_back(Item { Item::End, slice(keys, i, i + body), 0,
                                   { slice(keys, i + body, i + c.p), slice(keys, i + c.p + body, i + 2 * c.p) } });
            i += 2 * c.p;
        }
    }
    return items;
}

//! A cell of a row: a bar or a multi-bar rest
struct Cell {
    bool rest = false;
    BarKey key;
    double w = 1;                 // width in standard bars
    int n = 0;                    // rest: bars
    bool rs = false, re = false;  // repeat barlines
    int times = 0;                // repeat count (on the last bar of a repeat)
    int ending = 0;               // 1st/2nd ending starts here
    bool endingCont = false, endingLast = false;
    bool closing = false;
};

//! width of a bar, in standard bars: a 2/4 bar in a 4/4 song is 0.5 (never narrower than half, nor wider than 2)
double barWeight(const BarKey& key, const Fraction& unit)
{
    Fraction w = parseFrac(key.len) / unit;
    if (w < Fraction(1, 2)) {
        w = Fraction(1, 2);
    }
    if (w > Fraction(2, 1)) {
        w = Fraction(2, 1);
    }
    return double(w.numerator()) / double(w.denominator());
}

//! Rows holding up to `cols` standard bars
std::vector<std::vector<Cell> > buildRows(const std::vector<Item>& items, int cols, const Fraction& unit)
{
    std::vector<std::vector<Cell> > rows(1);
    auto used = [&]() {
        double s = 0;
        for (const Cell& c : rows.back()) {
            s += c.w;
        }
        return s;
    };
    auto newrow = [&]() {
        if (!rows.back().empty()) {
            rows.emplace_back();
        }
    };
    auto put = [&](const Cell& cell) {
        if (!rows.back().empty() && used() + cell.w > cols + 0.01) {
            rows.emplace_back();
        }
        rows.back().push_back(cell);
    };
    for (const Item& it : items) {
        if (it.kind == Item::BarItem) {
            Cell c;
            c.key = it.keys.front();
            c.w = barWeight(c.key, unit);
            put(c);
        } else if (it.kind == Item::Rest) {
            newrow();
            Cell c;
            c.rest = true;
            c.n = it.n;
            c.key = it.keys.front();
            c.w = double(cols);
            put(c);
            newrow();
        } else if (it.kind == Item::Rep) {
            newrow();
            const Keys& ks = it.keys;
            for (size_t j = 0; j < ks.size(); ++j) {
                Cell c;
                c.key = ks[j];
                c.w = barWeight(c.key, unit);
                c.rs = j == 0;
                c.re = j == ks.size() - 1;
                c.times = j == ks.size() - 1 ? it.n : 0;
                put(c);
            }
            newrow();
        } else {
            newrow();
            for (size_t j = 0; j < it.keys.size(); ++j) {
                Cell c;
                c.key = it.keys[j];
                c.w = barWeight(c.key, unit);
                c.rs = j == 0;
                put(c);
            }
            for (size_t n = 0; n < it.ends.size(); ++n) {
                const Keys& e = it.ends[n];
                for (size_t j = 0; j < e.size(); ++j) {
                    Cell c;
                    c.key = e[j];
                    c.w = barWeight(c.key, unit);
                    c.ending = j == 0 ? int(n) + 1 : 0;
                    c.endingCont = j > 0;
                    c.endingLast = j == e.size() - 1;
                    c.re = n == 0 && j == e.size() - 1;
                    put(c);
                }
            }
            newrow();
        }
    }
    if (rows.back().empty()) {
        rows.pop_back();
    }
    return rows;
}

QString barHtml(const Cell& cell, const BarKey* prev, bool showTsInside)
{
    const BarKey& k = cell.key;
    QStringList cls { "bar" };
    QString inner;
    if (showTsInside) {
        inner += tsHtml(k.ts);
        cls << "hasts";
    }
    if (cell.rest) {
        cls << "rest";
        inner += QString("<span class=\"mr\"><span class=\"mrn\">%1</span><span class=\"mrb\"></span></span>").arg(cell.n);
    } else if (prev && sameChords(prev->ch, k.ch) && prev->ts == k.ts && !k.ch.empty()) {
        inner += QString::fromUtf8("<span class=\"sim\">\xee\x94\x80</span>");   // U+E500 repeat1Bar
    } else if (!k.ch.empty()) {
        for (size_t idx = 0; idx < k.ch.size(); ++idx) {
            const Fraction at = k.ch[idx].first / parseFrac(k.len);
            inner += QString("<span class=\"slot%1\" style=\"--at:%2\">%3</span>")
                     .arg(idx == 0 ? " s0" : "", f3(double(at.numerator()) / double(at.denominator())), chordHtml(k.ch[idx].second));
        }
    }
    const bool tall = std::any_of(k.ch.begin(), k.ch.end(), [](const std::pair<Fraction, QString>& c) {
        return c.second != "NC" && !c.second.section('|', 2, 2).isEmpty();
    });
    if (tall) {
        cls << "tall";
    }
    if (cell.closing) {
        cls << "closing";
    }
    if (cell.rs) {
        cls << "rs";
    }
    if (cell.re) {
        cls << "re";
    }
    if (cell.rs) {
        inner += "<i class=\"dot d1 l\"></i><i class=\"dot d2 l\"></i>";
    }
    if (cell.re) {
        inner += "<i class=\"dot d1 r\"></i><i class=\"dot d2 r\"></i>";
    }
    if (cell.ending || cell.endingCont) {
        cls << "en";
        if (cell.ending) {
            cls << "enstart";
            inner += QString("<span class=\"enn\">%1.</span>").arg(cell.ending);
        }
        if (cell.endingLast && !cell.re) {
            cls << "enend";
        }
    }
    return QString("<div class=\"%1\" data-w=\"%2\">%3</div>").arg(cls.join(' '), f3(cell.w), inner);
}

QString tempoHtml(const QString& tempo)
{
    static const QRegularExpression ws("\\s+");
    static const QRegularExpression quarter("\\{metNoteQuarterUp\\}\\s*");
    static const QRegularExpression half("\\{metNoteHalfUp\\}\\s*");
    static const QRegularExpression braces("\\{[^}]*\\}");
    static const QRegularExpression equals("</span>\\s*=\\s*");
    QString t = tempo;
    t.replace(ws, " ");
    t = esc(t.trimmed());
    t.replace(quarter, QString::fromUtf8("<span class=\"note\">\xee\x87\x95</span>"));   // U+E1D5 metNoteQuarterUp
    t.replace(half, QString::fromUtf8("<span class=\"note\">\xee\x87\x93</span>"));      // U+E1D3 metNoteHalfUp
    t.remove(braces);
    t.replace(equals, QString::fromUtf8("</span>\xe2\x80\x89=\xe2\x80\x89"));              // thin spaces
    return t;
}
}

QString chordChartHtml(const ChordChartData& song, const QString& title, const ChordChartFonts& fonts, const QString& version)
{
    const Analysis a = analyse(song);
    // the song's usual bar length: the width of a standard bar (the most common one; the first of equals)
    Fraction unit(1, 1);
    {
        std::map<QString, int> counts;
        QStringList order;
        for (const ChordChartMeasure& m : song.measures) {
            if (!counts.count(m.len)) {
                order << m.len;
            }
            ++counts[m.len];
        }
        int best = -1;
        for (const QString& len : order) {
            if (counts[len] > best) {
                best = counts[len];
                unit = parseFrac(len);
            }
        }
    }
    const QString TIMES = QString::fromUtf8("\xc3\x97");       // ×
    const QString DASH = QString::fromUtf8("\xe2\x80\x93");    // –
    const QString DOT = QString::fromUtf8(" \xc2\xb7 ");       // " · "
    QStringList form;
    QString out;
    QString prevTs;   // null = none yet
    for (const Group& g : a.groups) {
        QStringList labs;
        for (const QString& l : g.labels) {
            labs << shortLabel(l);
        }
        const QString lab = labs.size() == 1 ? labs.first() : labs.first() + DASH + labs.last();
        const std::vector<Item> items = compressReadable(g.keys);
        std::vector<int> counts;
        for (const BarKey& k : g.keys) {
            if (!k.ch.empty()) {
                counts.push_back(int(k.ch.size()));
            }
        }
        double mean = 0;
        int maxCount = 0;
        for (int c : counts) {
            mean += c;
            maxCount = std::max(maxCount, c);
        }
        mean = counts.empty() ? 0 : mean / counts.size();
        const bool dense = !counts.empty() && (mean >= 1.75 || (maxCount >= 3 && mean >= 1.5));
        const int cols = dense ? 2 : 4;
        std::vector<std::vector<Cell> > rows = buildRows(items, cols, unit);
        QString reps;
        if (items.size() == 1 && items.front().kind == Item::Rep && labs.size() == 1) {
            reps = TIMES + QString::number(items.front().n);
        }
        if (!lab.isEmpty()) {
            form << lab + reps;
        }
        QStringList notes;
        if (labs.size() > 1) {
            notes << QString("%1 times").arg(labs.size());
        }
        if (g.labels.size() == 1) {
            if (const std::optional<QString> note = labelNote(g.labels.first())) {
                notes << *note;
            }
        }
        for (const QString& t : g.texts) {
            if (pyLen(t) < 80) {
                notes << t;
            }
        }
        QString sec = "<section>";
        const bool wide = pyLen(lab) > 2;
        if (wide || !notes.isEmpty()) {
            sec += QString("<div class=\"head\">%1%2</div>")
                   .arg(wide ? QString("<span class=\"lab\">%1</span>").arg(esc(lab)) : QString(),
                        notes.isEmpty() ? QString() : QString("<span class=\"notes\">%1</span>").arg(esc(notes.join(DOT))));
        }
        const BarKey* prev = nullptr;
        for (size_t r = 0; r < rows.size(); ++r) {
            std::vector<Cell>& row = rows[r];
            row.back().closing = true;
            const bool last = r == rows.size() - 1;
            // time signature: at the start of a row it sits in the margin, before the barline
            const QString firstTs = row.front().key.ts;
            QString marginTs;
            if (prevTs.isNull() || firstTs != prevTs) {
                marginTs = tsHtml(firstTs, "mts");
            }
            QStringList cells;
            for (size_t j = 0; j < row.size(); ++j) {
                const Cell& cell = row[j];
                const QString ts = cell.key.ts;
                const bool inside = j > 0 && ts != prevTs;
                cells << barHtml(cell, prev, inside);
                prev = cell.rest ? nullptr : &cell.key;
                prevTs = ts;
            }
            double used = 0;
            int times = 0;
            QStringList colsCss;
            for (const Cell& c : row) {
                used += c.w;
                if (!times && c.times) {
                    times = c.times;
                }
                colsCss << f3(c.w) + "fr";
            }
            if (used < cols - 0.01) {
                colsCss << f3(cols - used) + "fr";
                // a short row: the repeat count right after its repeat sign
                cells << QString("<div class=\"pad\">%1</div>")
                    .arg(times ? QString("<span class=\"cnt\">%1%2</span>").arg(QString::number(times), TIMES) : QString());
                times = 0;
            }
            const QString gutter = QString("<div class=\"gut\">%1%2</div>")
                                   .arg(r == 0 && !wide && !lab.isEmpty() ? QString("<span class=\"lab\">%1</span>").arg(esc(lab)) : QString(),
                                        marginTs);
            const QString right = QString("<div class=\"cnt\">%1</div>").arg(times ? QString::number(times) + TIMES : QString());
            sec += QString("<div class=\"row%1\" style=\"grid-template-columns: 44pt %2 20pt\">%3%4%5</div>")
                   .arg(last ? " last" : "", colsCss.join(' '), gutter, cells.join(QString()), right);
        }
        sec += "</section>";
        out += sec;
    }
    QString css = pyFormat(QString::fromUtf8(PDF_CSS), { { "modernoir", fonts.modernoir }, { "jost", fonts.jost },
                                                           { "bravura", fonts.bravura } });
    QString footer;
    if (!version.isEmpty()) {
        css += QString::fromUtf8(PDF_FOOTER_CSS);
        footer = QString("<div class=\"ver\">%1</div>").arg(esc(QString("Version %1 %2 made by StarScore Studio").arg(version, DOT.trimmed())));
    }
    QStringList formSpans;
    for (const QString& f : form) {
        formSpans << QString("<span>%1</span>").arg(esc(f));
    }
    return QString("<!doctype html><html><head><meta charset='utf-8'><style>%1</style></head><body><div id='page'>"
                   "<header><h1>%2</h1>%3<div class='form'>%4</div></header>%5%6</div>%7</body></html>")
           .arg(css, esc(title), a.tempo.isEmpty() ? QString() : QString("<div class=\"sub\">%1</div>").arg(tempoHtml(a.tempo)),
                formSpans.join(QString("<span class='sep'>%1</span>").arg(DOT)), out, footer, QString::fromUtf8(PDF_JS));
}

// ============================================================================================ 4. iReal Pro (chart.py)
namespace {
//! iReal Pro accepts only these qualities (iReal Pro custom chord chart protocol)
const std::set<QString>& irealQualities()
{
    static const std::set<QString> q = [] {
        std::set<QString> s;
        for (const QString& x : QString("5 2 add9 + o h sus ^ - ^7 -7 7 7sus h7 o7 ^9 ^13 6 69 ^7#11 ^9#11 ^7#5 -6 -69 -^7 -^9 -9 -11 -7b5 h9 "
                                        "-b6 -#5 9 7b9 7#9 7#11 7b5 7#5 9#11 9b5 9#5 7b13 7#9#5 7#9b5 7#9#11 7b9#11 7b9b5 7b9#5 7b9#9 7b9b13 7alt 13 13#11 "
                                        "13b9 13#9 7b9sus 7susadd3 9sus 13sus 7b13sus 11").split(' ', Qt::SkipEmptyParts)) {
            s.insert(x);
        }
        return s;
    }();
    return q;
}

//! MuseScore typed quality -> nearest iReal quality (approximations are listed in the page's notes)
const std::map<QString, QString>& irealMap()
{
    static const std::map<QString, QString> m {
        { "", "" }, { "m", "-" }, { "m7", "-7" }, { "^", "^" }, { "07", "h7" }, { "0", "h" }, { "o", "o" }, { "o7", "o7" },
        { "m^7", "-^7" }, { "m9", "-9" }, { "m11", "-11" }, { "m6", "-6" }, { "m69", "-69" }, { "m6/9", "-69" }, { "6/9", "69" },
        { "-9", "-9" }, { "m^9", "-^9" }, { "sus4", "sus" }, { "sus2", "2" }, { "madd9", "-" }, { "add11", "" }, { "o^7", "o" },
        { "m6b9", "-6" }, { "m6b9sus", "-6" }, { "6b9sus", "6" }, { "69sus", "69" }, { "13add11", "13" }, { "13b9#11", "13b9" },
        { "m7b9", "-7" }, { "7#9#5#11", "7#9#5" }, { "9#11", "9#11" }, { "11#9", "11" }, { "m^7#11", "-^7" }, { "m6#11", "-6" },
        { "madd9#11", "-" }, { "6#11", "6" }, { "^7#5", "^7#5" }, { "m2", "-" }, { "69no3", "69" }, { "(b6)", "" }, { "N.C", "" },
        { "N.C.", "" }, { "m7b5", "h7" }, { "-7b5", "h7" }, { "m7(b5)", "h7" }, { "maj7", "^7" }, { "-", "-" }, { "-7", "-7" },
        { "7b9#5", "7b9#5" }, { "^9", "^9" },
    };
    return m;
}

QString irealChord(const QString& key, std::set<QString>& notes)
{
    if (key == "NC") {
        return "n";
    }
    const QStringList parts = key.split('|');
    QString root = parts.value(0), bass = parts.value(2);
    const QString q = parts.value(1);
    QString iq;
    if (irealQualities().count(q)) {
        iq = q;
    } else if (irealMap().count(q)) {
        iq = irealMap().at(q);
        if (iq != q) {
            notes.insert(QString("%1%2 written as %3%4").arg(root, q, root, iq));
        }
    } else {
        iq = q;
        if (iq.startsWith('m')) {
            iq.replace(0, 1, "-");
        }
        if (!irealQualities().count(iq)) {
            // keep the chord's family: 7..., -..., ^..., else a plain triad
            const QString base = iq.startsWith("7") || iq.startsWith("9") || iq.startsWith("13") || iq.startsWith("11") ? "7"
                                 : iq.startsWith("-") ? "-7" : iq.startsWith("^") ? "^7" : "";
            notes.insert(QString("%1%2 written as %3%4").arg(root, q, root, base));
            iq = base;
        }
    }
    static const std::vector<std::pair<QString, QString> > SPELL {
        { "Cb", "B" }, { "Fb", "E" }, { "E#", "F" }, { "B#", "C" }, { "Bbb", "A" }, { "Ebb", "D" }, { "Abb", "G" }, { "Dbb", "C" },
        { "Gbb", "F" }
    };
    for (const auto& [a, b] : SPELL) {
        if (root == a) {
            root = b;
        }
        if (bass == a) {
            bass = b;
        }
    }
    return root + iq + (bass.isEmpty() ? QString() : "/" + bass);
}

const std::map<QString, QString> IREAL_TS {
    { "4/4", "T44" }, { "3/4", "T34" }, { "2/4", "T24" }, { "5/4", "T54" }, { "6/4", "T64" }, { "7/4", "T74" }, { "2/2", "T22" },
    { "3/2", "T32" }, { "5/8", "T58" }, { "6/8", "T68" }, { "7/8", "T78" }, { "9/8", "T98" }, { "12/8", "T12" }
};
const std::map<int, QString> KEYS_MAJOR {
    { -7, "Cb" }, { -6, "Gb" }, { -5, "Db" }, { -4, "Ab" }, { -3, "Eb" }, { -2, "Bb" }, { -1, "F" }, { 0, "C" }, { 1, "G" }, { 2, "D" },
    { 3, "A" }, { 4, "E" }, { 5, "B" }, { 6, "F#" }, { 7, "C#" }
};

//! the 4 cells of one bar (more chords: the bar takes more cells)
QStringList irealBarTokens(const BarKey& k, const BarKey* prev, std::set<QString>& notes)
{
    if (prev && sameChords(prev->ch, k.ch) && k.ch.size() == 1 && prev->ts == k.ts) {
        return { "x", " ", " ", " " };
    }
    const Fraction lnf = parseFrac(k.len);
    QStringList cells { " ", " ", " ", " " };
    if (k.ch.size() > 4) {
        QStringList all;
        for (const auto& [pos, kk] : k.ch) {
            all << irealChord(kk, notes);
        }
        return { all.join(','), " ", " ", " " };
    }
    for (const auto& [pos, kk] : k.ch) {
        const Fraction q = pos / lnf * Fraction(4, 1);
        int slot = std::min(3, q.numerator() / q.denominator());
        while (slot < 3 && cells[slot] != " ") {
            ++slot;
        }
        cells[slot] = cells[slot] == " " ? irealChord(kk, notes) : cells[slot] + "," + irealChord(kk, notes);
    }
    if (k.ch.empty()) {
        cells[0] = "x";
    }
    return cells;
}

//! iReal cells: a chord fills its cell, a space is an empty cell; chords in neighbouring cells are joined by a comma
QString cellsText(const QStringList& cells)
{
    QString out;
    bool prevChord = false;
    for (const QString& c : cells) {
        if (!c.trimmed().isEmpty()) {
            out += (prevChord ? "," : "") + c;
            prevChord = true;
        } else {
            out += " ";
            prevChord = false;
        }
    }
    return out;
}

struct IRealBar {
    QStringList tokens;
    QString open, close;
    int ending = 0;
    int times = 0;
    QString ts;
};

//! Each group as its lines of 16 cells
std::vector<std::pair<const Group*, QStringList> > irealSections(const Analysis& a, std::set<QString>& notes)
{
    std::vector<std::pair<const Group*, QStringList> > out;
    static const QStringList LETTERS { "A", "B", "C", "D" };
    int li = 0;
    QString prevTs;   // null = none yet
    for (size_t gi = 0; gi < a.groups.size(); ++gi) {
        const Group& g = a.groups[gi];
        const QString label = g.labels.first();
        const QString low = label.toLower();
        QString mark;
        if (low == "in" || low == "intro" || (gi == 0 && (low.isEmpty() || low == "i"))) {
            mark = "*i";
        } else if (low.startsWith('v')) {
            mark = "*V";
        } else {
            mark = "*" + LETTERS[li % 4];
            ++li;
        }
        QString text = g.labels.size() == 1 ? shortLabel(label) : shortLabel(g.labels.first()) + "-" + shortLabel(g.labels.last());
        if (g.labels.size() > 1) {
            text += QString(" (%1x)").arg(g.labels.size());
        }
        std::vector<Item> items = g.same ? compress(g.keys) : g.items;
        if (g.labels.size() > 1) {
            // the whole group played len(labels) times: one repeat around it when it is one plain block
            if (displayedBars(g.items) == int(g.keys.size()) && g.keys.size() <= 16) {
                items = { Item { Item::Rep, g.keys, int(g.labels.size()), {} } };
            } else {
                const std::vector<Item> once = items;
                items.clear();
                for (int n = 0; n < g.labels.size(); ++n) {
                    items.insert(items.end(), once.begin(), once.end());
                }
            }
        }
        std::vector<IRealBar> bars;
        const BarKey* prev = nullptr;
        for (const Item& it : items) {
            if (it.kind == Item::BarItem) {
                bars.push_back({ irealBarTokens(it.keys.front(), prev, notes), "|", "|", 0, 0, it.keys.front().ts });
                prev = &it.keys.front();
            } else if (it.kind == Item::Rep) {
                for (size_t j = 0; j < it.keys.size(); ++j) {
                    const bool lastBar = j == it.keys.size() - 1;
                    bars.push_back({ irealBarTokens(it.keys[j], prev, notes), j == 0 ? "{" : "|", lastBar ? "}" : "|", 0,
                                     lastBar && it.n > 2 ? it.n : 0, it.keys[j].ts });
                    prev = &it.keys[j];
                }
            } else {
                for (size_t j = 0; j < it.keys.size(); ++j) {
                    bars.push_back({ irealBarTokens(it.keys[j], prev, notes), j == 0 ? "{" : "|", "|", 0, 0, it.keys[j].ts });
                    prev = &it.keys[j];
                }
                for (size_t n = 0; n < it.ends.size(); ++n) {
                    const Keys& e = it.ends[n];
                    for (size_t j = 0; j < e.size(); ++j) {
                        const bool lastPass = n == it.ends.size() - 1;
                        bars.push_back({ irealBarTokens(e[j], prev, notes), "|", j == e.size() - 1 && !lastPass ? "}" : "|",
                                         j == 0 ? int(n) + 1 : 0, 0, e[j].ts });
                        prev = &e[j];
                    }
                }
            }
        }
        // barline before each bar, and after the last
        const int nb = int(bars.size());
        QStringList before;
        for (int k = 0; k < nb; ++k) {
            if (k == 0) {
                before << (bars[0].open == "{" ? "{" : "[");
            } else if (bars[k - 1].close == "}") {
                before << QString("}") + (bars[k].open == "{" ? "{" : "");
            } else if (bars[k].open == "{") {
                before << "{";
            } else {
                before << "|";
            }
        }
        const QString after = nb && bars[nb - 1].close == "}" ? "}" : "]";
        // lines of 4 bars (16 cells)
        QStringList lines;
        for (int s0 = 0; s0 < nb; s0 += 4) {
            const int chunk = std::min(4, nb - s0);
            QString txt;
            for (int j = 0; j < chunk; ++j) {
                const IRealBar& b = bars[s0 + j];
                const int k = s0 + j;
                QString pre;
                if (k == 0) {
                    pre += mark;
                }
                if (b.ts != prevTs) {
                    if (IREAL_TS.count(b.ts)) {
                        pre += IREAL_TS.at(b.ts);
                    } else {
                        static const std::map<QString, QString> NEAR {
                            { "7/16", "T78" }, { "3/8", "T34" }, { "5/16", "T58" }, { "11/8", "T12" }, { "11/16", "T12" }
                        };
                        const QString near = NEAR.count(b.ts) ? NEAR.at(b.ts) : "T44";
                        pre += near;
                        // (prototype: the note's wording, kept as it was)
                        notes.insert(QString("%1 time written as %2/%3").arg(b.ts, near.mid(1, 1), near != "T12" ? near.mid(2) : "12/8"));
                    }
                    prevTs = b.ts;
                }
                if (b.ending) {
                    pre += QString("N%1").arg(b.ending);
                }
                if (k == 0 && !text.isEmpty()) {
                    QString t = text;
                    t.remove('<');
                    t.remove('>');
                    pre += "<" + t + ">";
                }
                if (b.times) {
                    pre += QString("<%1x>").arg(b.times);
                }
                txt += before[k] + pre + cellsText(b.tokens);
            }
            if (s0 + 4 >= nb) {
                txt += after;
                if (chunk < 4) {
                    txt += QString(4 * (4 - chunk), ' ');   // empty cells: the next section starts on a new line
                }
            }
            lines << txt;
        }
        out.push_back({ &g, lines });
    }
    return out;
}

struct IRealLink {
    QString title;
    QString url;
    int lines = 0;
};

//! The charts: one, or several parts when the song is longer than 12 lines
std::vector<IRealLink> irealParts(const ChordChartData& song, const Analysis& a, const QString& title, const QString& composer,
                                  std::set<QString>& notes, int maxLines = 12)
{
    const auto secs = irealSections(a, notes);
    std::vector<std::vector<QStringList> > parts;
    std::vector<QStringList> cur;
    int used = 0;
    for (const auto& [g, lines] : secs) {
        if (used + lines.size() > maxLines && !cur.empty()) {
            parts.push_back(cur);
            cur.clear();
            used = 0;
        }
        if (lines.size() > maxLines) {
            // a section longer than one chart: split it by lines
            for (int s = 0; s < lines.size(); s += maxLines) {
                if (!cur.empty()) {
                    parts.push_back(cur);
                }
                cur = { lines.mid(s, maxLines) };
                used = cur.front().size();
            }
            continue;
        }
        cur.push_back(lines);
        used += lines.size();
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    const int keyNo = song.key ? *song.key : 0;
    const QString key = KEYS_MAJOR.count(keyNo) ? KEYS_MAJOR.at(keyNo) : "C";
    std::vector<IRealLink> links;
    for (size_t n = 0; n < parts.size(); ++n) {
        QString prog;
        int lineCount = 0;
        for (const QStringList& sec : parts[n]) {
            prog += sec.join(QString());
            lineCount += sec.size();
        }
        // rstrip()
        while (!prog.isEmpty() && prog.back().isSpace()) {
            prog.chop(1);
        }
        if (prog.endsWith(']')) {
            prog.chop(1);
            prog += "Z";
        }
        const QString t = parts.size() == 1 ? title : QString("%1 (Part %2 of %3)").arg(title).arg(n + 1).arg(parts.size());
        QString safeTitle = t;
        safeTitle.replace('=', '-');
        links.push_back({ t, "irealbook://" + QStringList { safeTitle, composer, "Funk", key, "n", prog }.join('='), lineCount });
    }
    return links;
}

//! render.py's composer_for_ireal: the first credit, surname first ("Joel McCullough" -> "McCullough Joel")
QString composerForIReal(const QString& c)
{
    QString first = c.section(" / ", 0, 0).trimmed();
    static const QRegularExpression prefix("^(arr\\.|music by|by)\\s+", QRegularExpression::CaseInsensitiveOption);
    first.remove(prefix);
    const QStringList parts = pySplit(first);
    if (parts.size() > 1) {
        return (parts.last() + " " + parts.mid(0, parts.size() - 1).join(' ')).trimmed();
    }
    return first.isEmpty() ? QString("Starsign") : first;
}
}

QString chordChartIRealHtml(const ChordChartData& song, const QString& title, const QString& version)
{
    const Analysis a = analyse(song);
    std::set<QString> notes;
    const std::vector<IRealLink> links = irealParts(song, a, title, composerForIReal(song.composer), notes);
    QString items;
    QStringList all;
    for (const IRealLink& l : links) {
        // (one arg() call: the quoted URL is full of "%2"s that a second call would take for placeholders)
        items += QString("<p><a href=\"%1\">%2</a> <small>(%3 lines)</small></p>").arg(urlQuote(l.url), esc(l.title), QString::number(l.lines));
        all << l.url.mid(QString("irealbook://").size());
    }
    const QString pl = links.size() > 1
                       ? QString("<p><a href=\"%1\"><b>All parts</b></a></p>").arg(urlQuote("irealbook://" + all.join('=')))
                       : QString();
    QString nt;
    for (const QString& n : notes) {   // sorted
        nt += QString("<li>%1</li>").arg(esc(n));
    }
    const QString footer = version.isEmpty() ? QString()
                           : QString("<p style='color:#888;font-size:12px;margin-top:24px'>%1</p>")
                           .arg(esc(QString::fromUtf8("Version %1 \xc2\xb7 made by StarScore Studio").arg(version)));
    return QString("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                   "<title>%1 - iReal Pro</title></head><body style='font-family:-apple-system,sans-serif;padding:16px'>"
                   "<h2>%2</h2><p>Tap a link to open it in iReal Pro.</p>%3%4%5%6</body></html>")
           .arg(esc(title), esc(title), pl, items,
                nt.isEmpty() ? QString() : QString("<h4>Written differently in iReal</h4><ul>%1</ul>").arg(nt), footer);
}

// ============================================================================================ 5. fonts and files
namespace {
QString dataUrl(const QByteArray& bytes, const QString& mime)
{
    return "data:" + mime + ";base64," + QString::fromLatin1(bytes.toBase64());
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}

namespace {
//! A font file: from `paths` (resources, the app bundle's fonts folder), else by name from the installed font folders
QByteArray fontFile(const QStringList& paths, const QStringList& namePatterns)
{
    for (const QString& p : paths) {
        const QByteArray bytes = readAll(p);
        if (!bytes.isEmpty()) {
            return bytes;
        }
    }
    for (const QString& dir : QStandardPaths::standardLocations(QStandardPaths::FontsLocation)) {
        QDirIterator it(dir, namePatterns, QDir::Files, QDirIterator::Subdirectories);
        QStringList found;
        while (it.hasNext()) {
            found << it.next();
        }
        found.sort();
        for (const QString& f : found) {
            // a static weight, not the variable font (its exported text came out regular, see font.cpp)
            if (!f.contains("Variable", Qt::CaseInsensitive)) {
                const QByteArray bytes = readAll(f);
                if (!bytes.isEmpty()) {
                    return bytes;
                }
            }
        }
    }
    return QByteArray();
}
}

ChordChartFonts chordChartEmbeddedFonts()
{
    ChordChartFonts fonts;
    const QString appFonts = QCoreApplication::applicationDirPath() + "/../Resources/fonts/";   // the macOS bundle
    // the two fonts StarScore ships: its resources, else the bundle's copies, else an installed copy
    fonts.jost = dataUrl(fontFile({ ":/fonts/starscorejost/StarScoreJost.ttf", appFonts + "StarScoreJost.ttf" },
                                  { "StarScoreJost.ttf" }), "font/ttf");
    fonts.bravura = dataUrl(fontFile({ ":/fonts/bravura/BravuraText.otf", appFonts + "BravuraText.otf" },
                                     { "BravuraText.otf" }), "font/otf");
    // TT Modernoir is installed on the Mac, not shipped (its trial licence doesn't allow passing the font on)
    QByteArray modernoir = fontFile({}, { "*Modernoir*Regular*.ttf", "*Modernoir*Regular*.otf" });
    if (modernoir.isEmpty()) {
        modernoir = fontFile({}, { "*Modernoir*.ttf", "*Modernoir*.otf" });
    }
    if (!modernoir.isEmpty()) {
        fonts.modernoir = dataUrl(modernoir, "font/ttf");
    } else {
        // no file found: the page's "src: url('…')" is completed into a list that falls back to the installed family
        fonts.modernoir = "data:,') format('truetype'), local('TT Modernoir Trial'), local('TT Modernoir";
    }
    return fonts;
}

namespace {
//! Prints the page with the organizer's WebKit printer and waits for it (the printer works through the main thread's
//! event loop, so this must be called on the main thread). False when the page couldn't be made.
bool renderChartPdf(const QString& html, const QString& pdfPath)
{
    if (!org::canRenderPdf()) {
        return false;
    }
    org::RenderJob job;
    job.html = html;
    job.pdfPath = pdfPath;
    job.marginTop = job.marginRight = job.marginBottom = job.marginLeft = 0;
    // prototype: one phone-width page (360 pt) as tall as the song needs; the page reports its height in
    // data-height (points) once its script has laid the chords out, and 780 pt is the least
    job.pageWidth = 360;
    job.pageHeight = 780;
    job.pageHeightFromHtml = true;
    struct State {
        QEventLoop loop;
        bool ok = false;
        bool finished = false;
    };
    auto state = std::make_shared<State>();
    org::renderPdfs({ job }, [state](int, bool ok) { state->ok = ok; }, [state]() {
        state->finished = true;
        state->loop.quit();
    });
    if (!state->finished) {
        // the printer's own watchdog gives up after 90 s; this one is for a callback that never comes
        QTimer::singleShot(120000, &state->loop, &QEventLoop::quit);
        state->loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return state->finished && state->ok && QFileInfo(pdfPath).size() > 0;
}

//! Writes `bytes` to `rel` under `folder` the way the sheets are written: under a temporary name first, the old file
//! archived, then renamed into place. Returns false (with a note in problems) when it couldn't.
bool placeFile(const QString& folder, const QString& rel, const QByteArray& bytes,
               const std::function<bool(const QString&, QString*)>& supersede, QStringList& problems)
{
    const QString target = folder + "/" + rel;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
        out.cancelWriting();
        problems << muse::qtrc("starscore", "%1: couldn't write the file.").arg(rel);
        return false;
    }
    QString archivedTo;
    if (!supersede(rel, &archivedTo)) {
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        if (!archivedTo.isEmpty()) {
            QFile::rename(archivedTo, target);   // the old file back where it was
        }
        problems << muse::qtrc("starscore", "%1: couldn't write the file.").arg(rel);
        return false;
    }
    return true;
}
}

ChordChartResult writeChordCharts(const MasterScore* ms, const QString& folder, const QString& code, const QString& title,
                                  const QString& version,
                                  const std::function<bool(const QString& relativePath, QString* archivedTo)>& supersede,
                                  const std::function<bool(const QByteArray& fresh, const QString& existingPath)>& samePdf,
                                  QStringList& problems)
{
    ChordChartResult result;
    const ChordChartData data = extractChordChart(ms);
    QString why;
    if (!chordChartPossible(data, &why)) {
        problems << muse::qtrc("starscore", "Chord chart: %1.").arg(why);
        return result;
    }
    const QString sub = "Chord Charts/";

    // --- the iReal Pro page: replaced only when its bytes differ
    {
        const QString rel = sub + code + " - iReal Pro.html";
        const QByteArray html = chordChartIRealHtml(data, title, version).toUtf8();
        const QString target = folder + "/" + rel;
        if (QFileInfo::exists(target) && readAll(target) == html) {
            result.unchanged << rel;
        } else if (placeFile(folder, rel, html, supersede, problems)) {
            result.written << rel;
        }
    }

    // --- the PDF (or, where it can't be printed, the page itself)
    const QString chartHtml = chordChartHtml(data, title, chordChartEmbeddedFonts(), version);
    if (!org::canRenderPdf()) {
        const QString rel = sub + code + " - Chord Chart.html";
        const QByteArray html = chartHtml.toUtf8();
        const QString target = folder + "/" + rel;
        if (QFileInfo::exists(target) && readAll(target) == html) {
            result.unchanged << rel;
        } else if (placeFile(folder, rel, html, supersede, problems)) {
            result.written << rel;
        }
        problems << muse::qtrc("starscore", "Chord chart: the PDF needs macOS; the page was written as %1 instead.")
            .arg(QFileInfo(rel).fileName());
        return result;
    }
    const QString rel = sub + code + " - Chord Chart.pdf";
    const QString tmp = QDir::tempPath() + QString("/StarScoreChordChart-%1.pdf").arg(QDateTime::currentMSecsSinceEpoch());
    if (!renderChartPdf(chartHtml, tmp)) {
        QFile::remove(tmp);
        problems << muse::qtrc("starscore", "Chord chart: the PDF couldn't be made.");
        return result;
    }
    const QByteArray pdf = readAll(tmp);
    QFile::remove(tmp);
    const QString target = folder + "/" + rel;
    // the same drawing as the file already there: it stays, nothing is archived
    if (QFileInfo::exists(target) && samePdf(pdf, target)) {
        result.unchanged << rel;
    } else if (placeFile(folder, rel, pdf, supersede, problems)) {
        result.written << rel;
    }
    return result;
}
}
