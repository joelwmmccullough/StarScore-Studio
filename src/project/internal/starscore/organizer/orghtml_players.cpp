/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: the per-player PDFs in each song folder:
 *   Update Notes/CODE - Changelog - <Player>.pdf   (the old gen_changelog.py)
 *   Horn Part Guides/CODE - <Player>.pdf          (the old gen_horn.py + hornguides.py)
 *
 * The horn guides used to be worked out by reading notes off the PDFs (score.py). Now StarScore analyses the
 * score itself when a song is exported (hornguides/CODE.json): every 1-, 2- and 3-horn part, rehearsal mark by
 * rehearsal mark, with written pitches. Songs that haven't been exported from StarScore keep their old guides.
 */
#include "orghtml.h"

#include <algorithm>
#include <set>

#include <QRegularExpression>

namespace mu::project::starscore::org {
static const char* PLAYER_CSS = R"CSS(
.hero{background:#16213e;color:#fff;margin:0 0 14px;padding:20px 24px 18px;border-radius:8px;}
.hero h1{color:#fff;font-size:24pt;margin:0;}
.hero .sub{color:#a9b4d0;font-size:10.5pt;margin-top:4px;}
.who{display:inline-block;background:#c8a24a;color:#16213e;font-weight:700;border-radius:4px;
     padding:2px 9px;font-size:10pt;margin-top:9px;}
.entry{border-left:3px solid #16213e;padding:0 0 0 14px;margin:16px 0 0;}
.entry.old{border-left-color:#cfd4e0;}
.stamp{display:flex;justify-content:space-between;align-items:baseline;
       border-bottom:1.5px solid #16213e;padding-bottom:3px;margin-bottom:7px;break-after:avoid;}
.stamp .d{font-size:13pt;font-weight:700;color:#16213e;font-family:'SF Mono',Menlo,Consolas,monospace;}
.stamp .t{font-size:8.4pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;}
.entry.old .stamp{border-bottom-color:#cfd4e0;} .entry.old .stamp .d{color:#6d7387;font-size:11.5pt;}
.chg{font-size:9.6pt;margin:5px 0;padding-left:16px;position:relative;break-inside:avoid;}
.chg:before{content:'';position:absolute;left:2px;top:5px;width:6px;height:6px;border-radius:50%;background:#c8a24a;}
.chg.ok:before{background:#2f7d5c;} .chg.warn:before{background:#a8391f;}
.fileref{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:8.8pt;color:#16213e;
         background:#f2f4f8;padding:1px 5px;border-radius:3px;}
.was{color:#8b90a0;font-size:8.8pt;}
.none{color:#6d7387;font-size:9.6pt;font-style:italic;}
.grid{width:100%;border-collapse:collapse;margin-top:6px;}
.grid th{font-size:8.2pt;text-transform:uppercase;letter-spacing:.6px;color:#6d7387;
   padding:0 6px 4px;border-bottom:1.5px solid #16213e;text-align:center;}
.grid th.l{text-align:left;}
.grid td{padding:3.5px 6px;font-size:9.2pt;text-align:center;border-bottom:1px solid #f0f2f7;}
.grid td.l{text-align:left;font-weight:700;color:#16213e;font-family:'SF Mono',Menlo,monospace;}
.grid tr.same{background:#f2f8f4;} .grid tr.differ{background:#fdf6e8;}
.grid tr.transposed{background:#eef3fa;}
.grid tr.tacet td{color:#b9bdc9;}
.pill{display:inline-block;border-radius:10px;padding:1px 8px;font-size:8.4pt;font-weight:700;}
.pill.same{background:#dcefe3;color:#1d6144;} .pill.differ{background:#f7e6c4;color:#7a5a12;}
.pill.transposed{background:#dbe5f4;color:#2b4b7a;}
.pill.tacet{background:#eef0f5;color:#8b90a0;}
.keyfact{background:#f7f8fb;border-left:3px solid #2f7d5c;padding:10px 13px;margin:10px 0;
   border-radius:0 5px 5px 0;font-size:9.6pt;break-inside:avoid;}
.keyfact.warn{border-left-color:#c8a24a;}
.diffbox{border:1px solid #e9dcc0;background:#fdfaf3;border-radius:6px;padding:9px 12px;margin:7px 0;break-inside:avoid;}
.diffbox .m{font-family:'SF Mono',Menlo,monospace;font-weight:700;color:#8a6d1f;font-size:11pt;}
.diffbox .d{font-size:9.4pt;color:#3d4356;margin-top:2px;}
)CSS";

// ------------------------------------------------------------------ changelog
QString changelogHtml(const SongInfo& song, const Player& player, const QJsonArray& entries)
{
    QString b = QString("<div class=\"hero\"><h1>%1</h1><div class=\"sub\">Changelog &middot; what has changed on your sheets, "
                        "newest first</div><div class=\"who\">%2 &nbsp;&middot;&nbsp; %3</div></div>")
                .arg(esc(song.name), esc(player.name), esc(player.blurb));
    if (entries.isEmpty()) {
        b += "<div class=\"none\">No changes recorded yet.</div>";
    }
    bool newest = true;
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        b += QString("<div class=\"entry%1\"><div class=\"stamp\"><span class=\"d\">%2</span><span class=\"t\">%3</span></div>")
             .arg(newest ? "" : " old", esc(e.value("date").toString()), esc(e.value("title").toString("update")));
        newest = false;
        const QJsonArray changes = e.value("changes").toArray();
        if (changes.isEmpty()) {
            b += "<div class=\"none\">Nothing changed on your sheets in this update.</div>";
        }
        for (const QJsonValue& c : changes) {
            const QJsonObject o = c.toObject();
            b += QString("<div class=\"chg %1\">%2</div>").arg(o.value("kind").toString(), o.value("text").toString());
        }
        b += "</div>";
    }
    b += QString("<div class=\"foot\"><span>Starsign &middot; Changelog &middot; %1</span>"
                 "<span>this PDF is auto-generated, do not edit it by hand</span></div>").arg(esc(player.name));
    return htmlPage(song.name + " - " + player.name + " changelog", b, PLAYER_CSS);
}

// ------------------------------------------------------------------ horn part guides
static QString pitchName(int midi)
{
    static const char* NAMES[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return QString("%1%2").arg(NAMES[((midi % 12) + 12) % 12]).arg(midi / 12 - 1);
}

static QString intervalName(int semitones)
{
    static const QStringList NAMES { "unison", "minor 2nd", "2nd", "minor 3rd", "3rd", "4th", "tritone", "5th", "minor 6th",
                                     "6th", "minor 7th", "7th", "octave" };
    const int n = std::abs(semitones);
    return n < NAMES.size() ? NAMES[n] : QString("%1 semitones").arg(n);
}

//! How far a horn's written notes sit above concert pitch
static int writtenOffset(const QString& instrument, const QString& key)
{
    static const QMap<QString, int> BY_INSTRUMENT { { "trumpet", 2 }, { "flugelhorn", 2 }, { "soprano sax", 2 }, { "clarinet", 2 },
        { "tenor sax", 14 }, { "bass clarinet", 14 }, { "alto sax", 9 }, { "bari sax", 21 }, { "trombone", 0 }, { "flute", 0 }, { "piccolo", -12 } };
    if (BY_INSTRUMENT.contains(instrument.toLower())) {
        return BY_INSTRUMENT.value(instrument.toLower());
    }
    return key == "Bb" ? 2 : key == "Eb" ? 9 : 0;
}

struct GuidePart {
    int n = 0;
    QString folder, label, instrument, trans, role = "primary";
    QStringList alts;
    int shift = 0;                 // added to every pitch (Any Horns chairs are analysed at concert pitch)
    QJsonObject data;              // the part in hornguides/CODE.json
};

struct Cell {
    bool present = false;
    int n = 0, restBars = 0, med = 0, lo = 0, hi = 0;
    std::vector<int> seq;
};

static Cell cellFor(const GuidePart& p, const QString& mark)
{
    Cell c;
    for (const QJsonValue& v : p.data.value("sections").toArray()) {
        const QJsonObject s = v.toObject();
        if (s.value("mark").toString() != mark) {
            continue;
        }
        c.present = true;
        c.n = s.value("n").toInt();
        c.restBars = s.value("restBars").toInt();
        for (const QJsonValue& x : s.value("seq").toArray()) {
            c.seq.push_back(x.toInt() + p.shift);
        }
        if (!c.seq.empty()) {
            std::vector<int> sorted = c.seq;
            std::sort(sorted.begin(), sorted.end());
            c.lo = sorted.front();
            c.hi = sorted.back();
            c.med = sorted[sorted.size() / 2];
        }
        break;
    }
    return c;
}

static std::vector<int> shape(const std::vector<int>& seq)
{
    std::vector<int> out;
    for (size_t i = 1; i < seq.size(); ++i) {
        out.push_back(seq[i] - seq[i - 1]);
    }
    return out;
}

//! "the 3-horn", or the folder when two of the player's parts are both 3-horn
static QString partName(const std::vector<GuidePart>& parts, int i)
{
    int same = 0;
    for (const GuidePart& p : parts) {
        same += p.n == parts[i].n;
    }
    return same > 1 ? parts[i].folder : QString("%1-horn").arg(parts[i].n);
}

static QString verdictFor(const std::vector<Cell>& row, const std::vector<GuidePart>& parts)
{
    int present = 0, playing = 0;
    for (const Cell& c : row) {
        present += c.present;
        playing += c.present && c.n > 0;
    }
    if (!present || !playing) {
        return "tacet";
    }
    if (present < int(row.size())) {
        return "differ";
    }
    std::set<std::vector<int> > seqs, shapes;
    std::set<QString> keys;
    for (size_t i = 0; i < row.size(); ++i) {
        seqs.insert(row[i].seq);
        shapes.insert(shape(row[i].seq));
        keys.insert(parts[i].trans);
    }
    if (seqs.size() == 1) {
        return "same";
    }
    if (shapes.size() == 1 && shapes.begin()->size() > 2) {
        return "transposed";
    }
    return "differ";
}

static QString describeDifference(const std::vector<Cell>& row, const std::vector<GuidePart>& parts)
{
    std::vector<int> playing, silent;
    for (size_t i = 0; i < row.size(); ++i) {
        if (!row[i].present) {
            continue;
        }
        (row[i].n > 0 ? playing : silent).push_back(int(i));
    }
    if (playing.empty()) {
        return QString();
    }
    if (playing.size() > 1) {
        std::set<std::vector<int> > shapes;
        for (int i : playing) {
            shapes.insert(shape(row[i].seq));
        }
        if (shapes.size() == 1 && shapes.begin()->size() > 2) {
            return "the same line in each, just written for a different horn - the shape is identical, only the key on the page changes";
        }
    }
    QStringList bits;
    auto nh = [&](int i) { return partName(parts, i); };
    if (!silent.empty()) {
        QStringList sn, pn;
        for (int i : silent) {
            sn << nh(i);
        }
        for (int i : playing) {
            pn << nh(i);
        }
        bits << QString("tacet in the %1, but you play in the %2").arg(sn.join(", "), pn.join(", "));
    } else if (playing.size() > 1) {
        const int hi = *std::max_element(playing.begin(), playing.end(), [&](int a, int b) { return row[a].n < row[b].n; });
        const int lo = *std::min_element(playing.begin(), playing.end(), [&](int a, int b) { return row[a].n < row[b].n; });
        if (row[hi].n >= row[lo].n * 2 && row[lo].n > 0) {
            bits << QString("far busier in the %1 (%2 notes vs %3)").arg(nh(hi)).arg(row[hi].n).arg(row[lo].n);
        } else if (row[hi].n != row[lo].n) {
            bits << QString("%1 notes in the %2 against %3 in the %4").arg(row[hi].n).arg(nh(hi)).arg(row[lo].n).arg(nh(lo));
        }
        // compare at concert pitch: written pitches differ by the instruments' transpositions
        const int top = *std::max_element(playing.begin(), playing.end(), [&](int a, int b) { return row[a].med < row[b].med; });
        const int bot = *std::min_element(playing.begin(), playing.end(), [&](int a, int b) { return row[a].med < row[b].med; });
        if (parts[top].trans == parts[bot].trans && row[top].med - row[bot].med >= 3) {
            bits << QString("sits about a %1 higher in the %2").arg(intervalName(row[top].med - row[bot].med), nh(top));
        }
        std::set<int> his;
        for (int i : playing) {
            his.insert(row[i].hi);
        }
        if (his.size() > 1) {
            const int t = *std::max_element(playing.begin(), playing.end(), [&](int a, int b) { return row[a].hi < row[b].hi; });
            bits << QString("highest note %1 (in the %2)").arg(pitchName(row[t].hi), nh(t));
        }
        if (bits.isEmpty()) {
            bits << "same number of notes but not the same line - worth a side-by-side look";
        }
    }
    return bits.join("; ");
}

//! Every part in the song's 1-, 2- and 3-horn arrangements that this player might read
static std::vector<GuidePart> partsForPlayer(const QJsonObject& analysis, const Player& player, const Roster& roster)
{
    std::vector<GuidePart> out;
    std::map<QString, std::vector<QJsonObject> > byFolder;
    QStringList folderOrder;
    for (const QJsonValue& v : analysis.value("parts").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("n").toInt() > 3) {
            continue;
        }
        const QString f = p.value("folder").toString();
        if (!byFolder.count(f)) {
            folderOrder << f;
        }
        byFolder[f].push_back(p);
    }
    static const QMap<QString, QString> TRANS { { "Trumpet", "Bb" }, { "Flugelhorn", "Bb" }, { "Tenor Sax", "Bb" }, { "Soprano Sax", "Bb" },
        { "Clarinet", "Bb" }, { "Bass Clarinet", "Bb" }, { "Alto Sax", "Eb" }, { "Bari Sax", "Eb" }, { "Piccolo", "C" }, { "Flute", "C" }, { "Trombone", "C" },
        { "Bass Trombone", "C" } };
    static const QRegularExpression num("\\s+\\d$");
    for (const QString& folder : folderOrder) {
        const auto& parts = byFolder[folder];
        const bool generic = parts.front().value("generic").toBool();
        const int n = parts.front().value("n").toInt();
        if (generic) {
            auto chair = player.chairs.find(n);
            if (chair == player.chairs.end()) {
                continue;
            }
            for (const QJsonObject& p : parts) {
                if (p.value("chair").toInt() != chair->second.chair) {
                    continue;
                }
                GuidePart g;
                g.n = n;
                g.folder = folder;
                // the roster's chair instrument is lower case ("alto sax"); it can also be blank for a new player
                QString inst = chair->second.instrument;
                if (!inst.isEmpty()) {
                    inst[0] = inst.at(0).toUpper();
                }
                g.instrument = inst.replace(" sax", " Sax");
                g.label = QString("Horn %1 - %2").arg(chair->second.chair).arg(g.instrument);
                g.trans = chair->second.key;
                g.shift = writtenOffset(chair->second.instrument, chair->second.key);
                g.data = p;
                out.push_back(g);
            }
            continue;
        }
        bool mine = false;
        for (const QJsonObject& p : parts) {
            const QString label = p.value("label").toString();
            if (roster.ownerOf(label) == player.name) {
                GuidePart g;
                g.n = n;
                g.folder = folder;
                g.label = QString(label).replace(QRegularExpression("^Tenor Sax\\s+[12]\\b"), "Tenor Sax");
                g.instrument = QString(label).remove(num);
                g.trans = TRANS.value(g.instrument, "?");
                g.data = p;
                out.push_back(g);
                mine = true;
            }
        }
        if (!mine) {
            // a chart with no part for this player: note what they could cover
            for (const QJsonObject& p : parts) {
                const QString label = p.value("label").toString();
                const QString base = QString(label).remove(num);
                if (player.instruments.contains(label) || player.instruments.contains(base)) {
                    GuidePart g;
                    g.n = n;
                    g.folder = folder;
                    g.label = label;
                    g.instrument = base;
                    g.trans = TRANS.value(base, "?");
                    g.role = "cover";
                    g.data = p;
                    out.push_back(g);
                    break;
                }
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const GuidePart& a, const GuidePart& b) {
        return a.n != b.n ? a.n < b.n : (a.role == "primary") > (b.role == "primary");
    });
    return out;
}

QString hornGuideHtml(const SongInfo& song, const Player& player, const QJsonObject& analysis, const Roster& roster)
{
    QString b = QString("<div class=\"hero\"><h1>%1</h1><div class=\"sub\">Horn part guide &middot; what is different between "
                        "your parts</div><div class=\"who\">%2 &nbsp;&middot;&nbsp; %3</div></div>")
                .arg(esc(song.name), esc(player.name), esc(player.blurb));
    QStringList bigger;
    for (const ArrangementInfo& a : song.arrangements) {
        if (a.n > 3) {
            bigger << a.folder;
        }
    }
    const QString foot = QString("<div class=\"foot\"><span>Starsign &middot; Horn part guide &middot; %1</span><span>made from "
                                 "the score (version %2) &middot; this PDF is auto-generated, do not edit it by hand</span></div>")
                         .arg(esc(player.name), esc(analysis.value("scoreVersion").toString("?")));
    const std::vector<GuidePart> parts = partsForPlayer(analysis, player, roster);
    if (parts.empty()) {
        b += "<div class=\"keyfact warn\"><b>Nothing to compare on this tune yet.</b> There is no 1-, 2- or 3-horn part for you in "
             + esc(song.name) + ".</div>";
        if (!bigger.isEmpty()) {
            b += "<p>The only horn charts that exist need more players than the band currently has:</p><ul>";
            for (const QString& f : bigger) {
                b += "<li><span class=\"fileref\">" + esc(f) + "</span></li>";
            }
            b += "</ul><p>Until a small-group chart is written, take the part from one of those that matches your instrument and "
                 "expect gaps where the missing horns would have been.</p>";
        }
        return htmlPage(song.name + " - " + player.name, b + foot, PLAYER_CSS);
    }

    // marks in the reading order of the score
    QStringList marks;
    for (const QJsonValue& v : analysis.value("marks").toArray()) {
        marks << v.toObject().value("mark").toString();
    }
    std::vector<std::vector<Cell> > grid;
    for (const QString& m : marks) {
        std::vector<Cell> row;
        for (const GuidePart& p : parts) {
            row.push_back(cellFor(p, m));
        }
        grid.push_back(row);
    }

    b += QString("<h2>Your %1 on this tune</h2>").arg(plural(int(parts.size()), "part"));
    b += "<table class=\"grid\"><tr><th class=\"l\">Arrangement</th><th class=\"l\">Your part</th><th>Key</th><th>Notes</th>"
         "<th>Written range</th></tr>";
    for (const GuidePart& p : parts) {
        int lo = 1000, hi = -1000;
        for (const QJsonValue& v : p.data.value("sections").toArray()) {
            for (const QJsonValue& x : v.toObject().value("seq").toArray()) {
                lo = std::min(lo, x.toInt() + p.shift);
                hi = std::max(hi, x.toInt() + p.shift);
            }
        }
        const QString range = hi < lo ? QString("&mdash;") : pitchName(lo) + "&ndash;" + pitchName(hi);
        b += QString("<tr><td class=\"l\">%1%2</td><td class=\"l\" style=\"font-weight:400;font-family:inherit;\">%3</td><td>%4</td>"
                     "<td>%5</td><td>%6</td></tr>")
             .arg(esc(p.folder), p.role == "primary" ? QString() : " <span class=\"dim\">(cover)</span>", esc(p.label), esc(p.trans))
             .arg(p.data.value("notes").toInt()).arg(range);
    }
    b += "</table>";

    QStringList same, trans, differ, tacet;
    QStringList verdicts;
    for (size_t i = 0; i < grid.size(); ++i) {
        const QString v = verdictFor(grid[i], parts);
        verdicts << v;
        (v == "same" ? same : v == "transposed" ? trans : v == "differ" ? differ : tacet) << marks[int(i)];
    }
    auto boldList = [](const QStringList& l) {
        QStringList out;
        for (const QString& m : l) {
            out << "<b>" + esc(m) + "</b>";
        }
        return out.join(", ");
    };
    if (parts.size() > 1) {
        if (differ.isEmpty()) {
            QStringList which;
            for (int i = 0; i < int(parts.size()); ++i) {
                which << "the " + partName(parts, i);
            }
            b += "<div class=\"keyfact\"><b>Good news &mdash; your parts are the same all the way through.</b> Every rehearsal mark "
                 "reads identically across " + which.join(" and ") + ". Learn it once.</div>";
        } else {
            const QString extra = trans.isEmpty() ? QString()
                                  : QString(" A further %1 (%2) are the same line written for a different horn.")
                                  .arg(trans.size()).arg(esc(trans.join(", ")));
            b += QString("<div class=\"keyfact\"><b>Only %1 of %2 sections actually differ: %3.</b><br>%4 are note-for-note identical "
                         "and %5 are tacet.%6 Learn the differences listed below and the rest carries over.</div>")
                 .arg(differ.size()).arg(marks.size()).arg(boldList(differ)).arg(same.size()).arg(tacet.size()).arg(extra);
        }
    }

    b += "<h2>Section by section</h2><table class=\"grid\"><tr><th class=\"l\" style=\"width:52px\">Mark</th>";
    for (int i = 0; i < int(parts.size()); ++i) {
        b += QString("<th>%1<br><span style=\"font-weight:400;text-transform:none;letter-spacing:0;\">%2</span></th>")
             .arg(esc(partName(parts, i)), esc(parts[i].instrument));
    }
    b += "<th class=\"l\">Verdict</th></tr>";
    static const QMap<QString, QString> LABEL { { "same", "identical" }, { "transposed", "same line, transposed" },
        { "differ", "DIFFERENT" }, { "tacet", "tacet" } };
    for (size_t i = 0; i < grid.size(); ++i) {
        const QString v = verdicts[int(i)];
        b += QString("<tr class=\"%1\"><td class=\"l\">%2</td>").arg(v, esc(marks[int(i)]));
        for (const Cell& c : grid[i]) {
            if (!c.present) {
                b += "<td class=\"dim\">&mdash;</td>";
            } else if (c.n == 0) {
                b += QString("<td class=\"dim\">%1</td>").arg(c.restBars ? QString("tacet %1 bars").arg(c.restBars) : QString("tacet"));
            } else {
                b += QString("<td>%1 <span class=\"dim\" style=\"font-size:8pt;\">%2&ndash;%3</span></td>")
                     .arg(c.n).arg(pitchName(c.lo), pitchName(c.hi));
            }
        }
        b += QString("<td class=\"l\" style=\"font-weight:400;font-family:inherit;\"><span class=\"pill %1\">%2</span></td></tr>")
             .arg(v, LABEL.value(v));
    }
    b += "</table><div class=\"dim\" style=\"font-size:8.5pt;margin-top:5px;\">Numbers are how many notes you play in that section, "
         "with the written range beside them. &ldquo;Identical&rdquo; means the written notes are exactly the same in every one of "
         "your parts.</div>";

    if (!differ.isEmpty() || !trans.isEmpty()) {
        b += "<h2>What to watch for</h2>";
        for (size_t i = 0; i < grid.size(); ++i) {
            if (verdicts[int(i)] != "differ" && verdicts[int(i)] != "transposed") {
                continue;
            }
            const QString desc = describeDifference(grid[i], parts);
            if (!desc.isEmpty()) {
                b += QString("<div class=\"diffbox\"><span class=\"m\">%1</span><div class=\"d\">%2</div></div>")
                     .arg(esc(marks[int(i)]), esc(desc));
            }
        }
    }
    if (parts.size() == 1) {
        int playingCount = 0, best = -1;
        QStringList resting;
        for (size_t i = 0; i < grid.size(); ++i) {
            const Cell& c = grid[i][0];
            if (c.present && c.n > 0) {
                ++playingCount;
                if (best < 0 || c.n > grid[best][0].n) {
                    best = int(i);
                }
            } else if (c.present) {
                resting << marks[int(i)];
            }
        }
        if (best >= 0) {
            const Cell& c = grid[best][0];
            b += QString("<div class=\"keyfact\">Only one part for you on this tune. You play in %1 of %2 sections; the busiest is "
                         "<b>%3</b> (%4 notes, %5&ndash;%6).%7</div>")
                 .arg(playingCount).arg(marks.size()).arg(esc(marks[best])).arg(c.n).arg(pitchName(c.lo), pitchName(c.hi))
                 .arg(resting.isEmpty() ? QString() : " You are tacet at " + esc(resting.join(", ")) + ".");
        }
    }
    if (!bigger.isEmpty()) {
        QStringList refs;
        for (const QString& f : bigger) {
            refs << "<span class=\"fileref\">" + esc(f) + "</span>";
        }
        b += "<h2>Bigger charts, for reference</h2><p style=\"font-size:9.4pt;\">This tune also has " + refs.join(", ")
             + ". Not covered here &mdash; those need more horns than the band currently carries.</p>";
    }
    return htmlPage(song.name + " - " + player.name, b + foot, PLAYER_CSS);
}
}
