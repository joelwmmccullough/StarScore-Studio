/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: cutting an exercise's MusicXML (see starscorepracticexml.h)
 */
#include "starscorepracticexml.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

#include <QDomDocument>
#include <QDomElement>
#include <QRegularExpression>

using namespace mu::project;

namespace {
const QStringList STATE_TAGS { "divisions", "key", "time", "staves", "clef", "transpose", "staff-details" };

QList<QDomElement> childElements(const QDomElement& e, const QString& tag = QString())
{
    QList<QDomElement> out;
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (tag.isEmpty() || c.tagName() == tag) {
            out << c;
        }
    }
    return out;
}

//! A state element's slot: "clef1", "key", "time", ... (clefs and keys per staff number)
QString slotOf(const QDomElement& e)
{
    return e.tagName() + e.attribute("number");
}

//! The attributes in force at the start of measure `index` (0-based) of a part
std::map<QString, QDomElement> stateBefore(const QList<QDomElement>& measures, int index)
{
    std::map<QString, QDomElement> state;
    for (int i = 0; i < index && i < measures.size(); ++i) {
        for (const QDomElement& at : childElements(measures[i], "attributes")) {
            for (const QDomElement& c : childElements(at)) {
                if (STATE_TAGS.contains(c.tagName())) {
                    state[slotOf(c)] = c;
                }
            }
        }
    }
    return state;
}

//! Puts the state into the measure's first <attributes> (making one), not replacing what the measure sets itself
void applyState(QDomDocument& doc, QDomElement& measure, const std::map<QString, QDomElement>& state)
{
    QDomElement at = measure.firstChildElement("attributes");
    if (at.isNull()) {
        at = doc.createElement("attributes");
        // after a <print>, before the notes
        QDomElement first = measure.firstChildElement();
        while (!first.isNull() && first.tagName() == "print") {
            first = first.nextSiblingElement();
        }
        if (first.isNull()) {
            measure.appendChild(at);
        } else {
            measure.insertBefore(at, first);
        }
    }
    std::map<QString, bool> has;
    for (const QDomElement& c : childElements(at)) {
        has[slotOf(c)] = true;
    }
    // MusicXML's order: divisions, key, time, staves, ..., clef, staff-details, transpose
    static const QStringList ORDER { "divisions", "key", "time", "staves", "clef", "staff-details", "transpose" };
    for (const QString& tag : ORDER) {
        for (const auto& [slot, el] : state) {
            if (el.tagName() != tag || has[slot]) {
                continue;
            }
            QDomNode copy = el.cloneNode(true);
            // before the first existing child that comes later in the order
            QDomElement before;
            for (const QDomElement& c : childElements(at)) {
                if (ORDER.indexOf(c.tagName()) > ORDER.indexOf(tag)) {
                    before = c;
                    break;
                }
            }
            if (before.isNull()) {
                at.appendChild(copy);
            } else {
                at.insertBefore(copy, before);
            }
        }
    }
}

//! Removes repeats, endings, jumps, and (when asked) tempo marks from a copied measure
void cleanMeasure(QDomElement& m, bool dropTempo)
{
    QList<QDomElement> remove;
    for (const QDomElement& c : childElements(m)) {
        if (c.tagName() == "barline" && (!c.firstChildElement("repeat").isNull() || !c.firstChildElement("ending").isNull())) {
            remove << c;
        } else if (c.tagName() == "direction") {
            const QDomElement dt = c.firstChildElement("direction-type");
            const bool jump = !dt.firstChildElement("segno").isNull() || !dt.firstChildElement("coda").isNull();
            const bool metronome = !dt.firstChildElement("metronome").isNull();
            if (jump || (dropTempo && metronome)) {
                remove << c;
                continue;
            }
            QDomElement snd = c.firstChildElement("sound");
            if (!snd.isNull()) {
                for (const char* a : { "dacapo", "dalsegno", "segno", "coda", "tocoda", "fine" }) {
                    snd.removeAttribute(a);
                }
                if (dropTempo) {
                    snd.removeAttribute("tempo");
                }
            }
        } else if (c.tagName() == "sound") {
            QDomElement snd = c;
            for (const char* a : { "dacapo", "dalsegno", "segno", "coda", "tocoda", "fine" }) {
                snd.removeAttribute(a);
            }
            if (dropTempo) {
                snd.removeAttribute("tempo");
            }
        } else if (c.tagName() == "print") {
            QDomElement pr = c;
            pr.removeAttribute("new-page");
        }
    }
    for (QDomElement& c : remove) {
        m.removeChild(c);
    }
}

//! A tempo mark: "♩ = n" (in the exercise's beat unit), playing at quarters per minute
QDomElement tempoDirection(QDomDocument& doc, const QString& beatUnit, bool dotted, int perMinute, double quarterBpm)
{
    QDomElement d = doc.createElement("direction");
    d.setAttribute("placement", "above");
    QDomElement dt = doc.createElement("direction-type");
    QDomElement mt = doc.createElement("metronome");
    QDomElement bu = doc.createElement("beat-unit");
    bu.appendChild(doc.createTextNode(beatUnit));
    mt.appendChild(bu);
    if (dotted) {
        mt.appendChild(doc.createElement("beat-unit-dot"));
    }
    QDomElement pm = doc.createElement("per-minute");
    pm.appendChild(doc.createTextNode(QString::number(perMinute)));
    mt.appendChild(pm);
    dt.appendChild(mt);
    d.appendChild(dt);
    QDomElement snd = doc.createElement("sound");
    snd.setAttribute("tempo", QString::number(quarterBpm, 'f', 2));
    d.appendChild(snd);
    return d;
}

double quartersPerBeatUnit(const QString& unit, bool dotted)
{
    static const std::map<QString, double> Q { { "whole", 4 }, { "half", 2 }, { "quarter", 1 }, { "eighth", 0.5 }, { "16th", 0.25 } };
    auto it = Q.find(unit);
    const double q = it == Q.end() ? 1.0 : it->second;
    return dotted ? q * 1.5 : q;
}

//! Length of measures in quarter notes, from the time signatures
double quartersOf(const QList<QDomElement>& measures, int from, int to, const std::map<QString, QDomElement>& state)
{
    double beats = 4, beatType = 4;
    auto t = state.find("time");
    if (t != state.end()) {
        beats = 0;
        for (const QString& b : t->second.firstChildElement("beats").text().split('+')) {   // ("2+2+3")
            beats += b.toDouble();
        }
        beatType = std::max(1.0, t->second.firstChildElement("beat-type").text().toDouble());
    }
    double q = 0;
    for (int i = from; i <= to && i < measures.size(); ++i) {
        for (const QDomElement& at : childElements(measures[i], "attributes")) {
            const QDomElement time = at.firstChildElement("time");
            if (!time.isNull()) {
                beats = 0;
                for (const QString& b : time.firstChildElement("beats").text().split('+')) {
                    beats += b.toDouble();
                }
                beatType = std::max(1.0, time.firstChildElement("beat-type").text().toDouble());
            }
        }
        q += beats * 4.0 / beatType;
    }
    return q;
}

struct Parsed {
    QDomDocument doc;
    QDomElement root;
    QList<QDomElement> parts;
    bool ok = false;
};

Parsed parse(const QString& xml)
{
    Parsed p;
    if (!p.doc.setContent(xml)) {
        return p;
    }
    p.root = p.doc.documentElement();
    p.parts = childElements(p.root, "part");
    p.ok = !p.parts.isEmpty();
    return p;
}

//! A new score with the old one's header (part-list filtered) and empty parts, keeping only the parts `keep` says
QDomDocument shell(const Parsed& src, const std::function<bool(const QString&)>& keep, std::map<QString, QDomElement>& newParts)
{
    QDomDocument doc;
    doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
    QDomElement root = doc.createElement("score-partwise");
    root.setAttribute("version", src.root.attribute("version", "4.0"));
    doc.appendChild(root);
    for (const QDomElement& c : childElements(src.root)) {
        if (c.tagName() == "part") {
            continue;
        }
        if (c.tagName() == "part-list") {
            QDomElement pl = doc.createElement("part-list");
            for (const QDomElement& pc : childElements(c)) {
                if (pc.tagName() != "score-part" || keep(pc.attribute("id"))) {
                    pl.appendChild(doc.importNode(pc, true));
                }
            }
            root.appendChild(pl);
            continue;
        }
        if (c.tagName() == "credit") {
            continue;
        }
        root.appendChild(doc.importNode(c, true));
    }
    for (const QDomElement& part : src.parts) {
        const QString id = part.attribute("id");
        if (!keep(id)) {
            continue;
        }
        QDomElement np = doc.createElement("part");
        np.setAttribute("id", id);
        root.appendChild(np);
        newParts[id] = np;
    }
    return doc;
}
}

bool mu::project::practice::isBackingPartId(const QString& id)
{
    return id == "P2" || id == "P3" || (id.size() >= 2 && id.at(0) == 'B' && id.at(1).isDigit());
}

bool mu::project::practice::hasBacking(const QString& musicXml)
{
    static const QRegularExpression rx("<part id=\"(P2|P3|B\\d+)\"");
    return rx.match(musicXml).hasMatch();
}

int mu::project::practice::barCount(const QString& musicXml)
{
    Parsed p = parse(musicXml);
    return p.ok ? int(childElements(p.parts.front(), "measure").size()) : 0;
}

QString mu::project::practice::sliceExercise(const QString& musicXml, int from, int to, bool backingOnly)
{
    Parsed src = parse(musicXml);
    if (!src.ok) {
        return QString();
    }
    const int n = int(childElements(src.parts.front(), "measure").size());
    from = from <= 0 ? 1 : std::min(from, n);
    to = to <= 0 ? n : std::clamp(to, from, n);
    std::map<QString, QDomElement> newParts;
    QDomDocument doc = shell(src, [backingOnly](const QString& id) { return !backingOnly || isBackingPartId(id); }, newParts);
    if (newParts.empty()) {
        return QString();
    }
    for (const QDomElement& part : src.parts) {
        auto np = newParts.find(part.attribute("id"));
        if (np == newParts.end()) {
            continue;
        }
        const QList<QDomElement> measures = childElements(part, "measure");
        const auto state = stateBefore(measures, from - 1);
        int number = 0;
        for (int i = from - 1; i <= to - 1 && i < measures.size(); ++i) {
            QDomElement m = doc.importNode(measures[i], true).toElement();
            m.setAttribute("number", QString::number(++number));
            m.removeAttribute("implicit");
            cleanMeasure(m, false);
            if (i == from - 1) {
                applyState(doc, m, state);
            }
            np->second.appendChild(m);
        }
    }
    return doc.toString(1);
}

mu::project::practice::PlayAlong mu::project::practice::playAlong(const QString& musicXml, int from, int to, int startTempo,
                                                                   int goalTempo, int seconds)
{
    PlayAlong out;
    Parsed src = parse(musicXml);
    if (!src.ok || !hasBacking(musicXml)) {
        return out;
    }
    const QList<QDomElement> first = childElements(src.parts.front(), "measure");
    const int n = int(first.size());
    if (n == 0) {
        return out;
    }
    from = from <= 0 ? 1 : std::min(from, n);
    to = to <= 0 ? n : std::clamp(to, from, n);

    // the exercise's tempo mark: its beat unit and number
    QString beatUnit = "quarter";
    bool dotted = false;
    int ownTempo = 0;
    {
        const QDomNodeList mets = src.doc.elementsByTagName("metronome");
        if (!mets.isEmpty()) {
            const QDomElement mt = mets.at(0).toElement();
            beatUnit = mt.firstChildElement("beat-unit").text().trimmed();
            dotted = !mt.firstChildElement("beat-unit-dot").isNull();
            ownTempo = mt.firstChildElement("per-minute").text().toInt();
        }
        if (beatUnit.isEmpty()) {
            beatUnit = "quarter";
        }
    }
    const double qpb = quartersPerBeatUnit(beatUnit, dotted);
    int t0 = startTempo > 0 ? startTempo : ownTempo;
    const int goal = std::max(goalTempo > 0 ? goalTempo : t0, t0);
    const double quarters = quartersOf(first, from - 1, to - 1, stateBefore(first, from - 1));
    auto passSeconds = [&](int tempo) {
        const double qbpm = tempo > 0 ? tempo * qpb : 100.0;
        return quarters * 60.0 / std::max(20.0, qbpm);
    };
    const int step = goal > t0 && t0 > 0 ? std::max(2, int(std::lround((goal - t0) / 5.0))) : 0;

    // the passes: tempo and how many times through
    std::vector<std::pair<int, int> > passes;
    std::vector<double> starts;
    double total = 0;
    int bars = 0;
    const int len = to - from + 1;
    for (int p = 0; total < std::max(30, seconds) && bars < 800; ++p) {
        const int tempo = t0 > 0 ? std::min(goal, t0 + p * step) : 0;
        const double one = std::max(0.5, passSeconds(tempo));
        const int reps = std::max(1, int(std::ceil(20.0 / one)));
        passes.push_back({ tempo, reps });
        starts.push_back(total);
        total += reps * one;
        bars += reps * len;
    }

    std::map<QString, QDomElement> newParts;
    QDomDocument doc = shell(src, [](const QString& id) { return isBackingPartId(id); }, newParts);
    bool firstBacking = true;
    for (const QDomElement& part : src.parts) {
        auto np = newParts.find(part.attribute("id"));
        if (np == newParts.end()) {
            continue;
        }
        const QList<QDomElement> measures = childElements(part, "measure");
        const auto state = stateBefore(measures, from - 1);
        // key, time, or clef changes inside the bars: each time through starts from the first bar's state again
        bool changes = false;
        for (int i = from; i <= to - 1 && i < measures.size(); ++i) {
            for (const QDomElement& at : childElements(measures[i], "attributes")) {
                changes = changes || !at.firstChildElement("key").isNull() || !at.firstChildElement("time").isNull()
                          || !at.firstChildElement("clef").isNull();
            }
        }
        int number = 0;
        for (size_t p = 0; p < passes.size(); ++p) {
            for (int r = 0; r < passes[p].second; ++r) {
                for (int i = from - 1; i <= to - 1 && i < measures.size(); ++i) {
                    QDomElement m = doc.importNode(measures[i], true).toElement();
                    m.setAttribute("number", QString::number(++number));
                    m.removeAttribute("implicit");
                    cleanMeasure(m, t0 > 0);
                    if (i == from - 1 && (number == 1 || changes)) {
                        applyState(doc, m, state);
                    }
                    if (i == from - 1 && r == 0 && firstBacking && passes[p].first > 0) {
                        // the pass's tempo, after the attributes
                        QDomElement at = m.firstChildElement("attributes");
                        const QDomElement d = tempoDirection(doc, beatUnit, dotted, passes[p].first, passes[p].first * qpb);
                        if (at.isNull()) {
                            m.insertBefore(d, m.firstChild());
                        } else {
                            m.insertAfter(d, at);
                        }
                    }
                    if (i == from - 1 && r == 0 && p > 0) {
                        QDomElement pr = doc.createElement("print");
                        pr.setAttribute("new-system", "yes");
                        m.insertBefore(pr, m.firstChild());
                    }
                    np->second.appendChild(m);
                }
            }
        }
        firstBacking = false;
    }
    out.xml = doc.toString(1);
    out.seconds = int(std::lround(total));
    if (t0 > 0) {
        for (size_t p = 0; p < passes.size(); ++p) {
            if (out.passTempos.isEmpty() || out.passTempos.back() != passes[p].first) {
                out.passTempos << passes[p].first;
                out.passStarts << int(std::lround(starts[p]));
            }
        }
    }
    return out;
}
