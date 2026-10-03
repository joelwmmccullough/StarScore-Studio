/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: changelog entries
 */
#include "orgchangelog.h"

#include <set>

#include <QJsonArray>
#include <QRegularExpression>

#include "orghtml.h"

namespace mu::project::starscore::org {
QString barRanges(const std::vector<std::pair<int, int> >& bars)
{
    QStringList out;
    for (const auto& [a, b] : bars) {
        out << (a == b ? QString::number(a) : QString("%1–%2").arg(a).arg(b));
    }
    return out.join(", ");
}

//! "AMPL - Alto Sax.pdf" -> "Alto Sax"; "AMPL - Horn 2 - Alto Sax.pdf" -> "Alto Sax"; "AMPL - Bass (Name).pdf" -> "Bass (Name)"
static QString partOfFile(const QString& file, const QString& code)
{
    QString b = file.section('/', -1);
    b.remove(QRegularExpression("^" + QRegularExpression::escape(code) + "\\s*-\\s*"));
    b.remove(QRegularExpression("\\.pdf$", QRegularExpression::CaseInsensitiveOption));
    const QRegularExpressionMatch chair = QRegularExpression("^Horn \\d+ - (.+)$").match(b);
    if (chair.hasMatch()) {
        b = chair.captured(1);
    }
    b.remove(QRegularExpression(" in (Bb|Eb|C|F|A)$"));
    return b.trimmed();
}

static QStringList readersOfSheet(const QString& relInSong, const QString& code, const Roster& roster)
{
    const QString folder = relInSong.section('/', 0, 0);
    const QString file = relInSong.section('/', -1);
    if (file.contains(" - Score.pdf") || folder == "Reference PDFs") {
        return {};
    }
    if (folder == "1 Lead Sheet") {
        QStringList all;
        for (const Player* p : roster.current()) {
            all << p->name;
        }
        return all;
    }
    return roster.readersOf(partOfFile(file, code));
}

static QString fileref(const QString& relInSong)
{
    return "<span class=\"fileref\">" + esc(relInSong) + "</span>";
}

static QString folderLabel(const QString& relInSong)
{
    const QString folder = relInSong.section('/', 0, 0);
    if (folder == "1 Lead Sheet") {
        return "Lead Sheet";
    }
    if (folder == "1 Rhythm") {
        return "Rhythm Section";
    }
    if (folder == "1H") {
        return "1-Horn Arrangement";
    }
    const QRegularExpressionMatch m = QRegularExpression("^(\\d)H (Any)?").match(folder);
    if (m.hasMatch()) {
        return m.captured(2).isEmpty() ? QString("%1-Horn Arrangement").arg(m.captured(1))
               : QString("Flexible %1-Horn Arrangement").arg(m.captured(1));
    }
    return folder;
}

int addChangelogEntries(QJsonObject& changelog, const SongInfo& song, const Roster& roster, const ExportInfo* exported,
                        const QStringList& added, const QStringList& changed, const QStringList& removed, const QDate& today)
{
    std::map<QString, QJsonArray> changes;    // player -> changes
    std::set<QString> described;              // sheets (relative to the song folder) covered by the export
    struct ChairGroup {
        SheetChange change;
        QString arrangement;
        QStringList files;
    };
    std::map<std::pair<QString, QString>, ChairGroup> chairGroups;   // (player, folder|chair)
    const QString prefix = song.root + "/";
    auto addFor = [&](const QStringList& players, const QString& text, const QString& kind) {
        for (const QString& p : players) {
            QJsonArray& a = changes[p];
            for (const QJsonValue& v : a) {
                if (v.toObject().value("text").toString() == text) {
                    return;
                }
            }
            a.append(QJsonObject { { "text", text }, { "kind", kind } });
        }
    };

    if (exported) {
        // Any Horns chair versions: one line per chair, to whoever can read any version of it
        for (const SheetChange& c : exported->sheets) {
            described.insert(c.relativePath);
            if (c.isScore || c.kind == SheetChange::Same) {
                continue;
            }
            const QStringList who = readersOfSheet(c.relativePath, song.code, roster);
            if (who.isEmpty()) {
                continue;
            }
            const QString arr = c.arrangement.isEmpty() ? folderLabel(c.relativePath) : c.arrangement;
            const bool lead = c.relativePath.startsWith("1 Lead Sheet/");
            // Any Horns: every version of a chair says the same thing; one line per chair
            const QRegularExpressionMatch chair = QRegularExpression("^Horn (\\d+)").match(c.part);
            if ((c.relativePath.section('/', 0, 0).contains("Any Horns") || c.relativePath.section('/', 0, 0).contains("Flexible")) && chair.hasMatch()) {
                const QString key = c.relativePath.section('/', 0, 0) + "|" + chair.captured(1);
                for (const QString& p : who) {
                    auto& g = chairGroups[{ p, key }];
                    g.change = c;
                    g.arrangement = arr;
                    g.files << c.relativePath.section('/', -1);
                }
                continue;
            }
            QString text;
            if (c.kind == SheetChange::Added) {
                text = lead ? QString("New <b>lead sheet</b>: %1").arg(fileref(c.relativePath))
                       : QString("New: a <b>%1</b> sheet for your <b>%2</b> part &mdash; %3").arg(esc(arr), esc(c.part), fileref(c.relativePath));
                addFor(who, text, "ok");
                continue;
            }
            const QString where = c.bars.empty() ? QString()
                                  : QString(" in bar%1 %2").arg(c.bars.size() == 1 && c.bars[0].first == c.bars[0].second ? "" : "s",
                                                                barRanges(c.bars))
                                  + (c.letters.isEmpty() ? QString()
                                     : QString(" (letter%1 %2)").arg(c.letters.size() > 1 ? "s" : "", esc(c.letters.join(", "))));
            if (!c.barsKnown) {
                text = lead ? QString("The <b>lead sheet</b> was updated. %1").arg(fileref(c.relativePath))
                       : QString("<b>%1</b>: your %2 sheet was updated. %3").arg(esc(arr), esc(c.part), fileref(c.relativePath));
                addFor(who, text, "warn");
            } else if (c.bars.empty()) {
                continue;
            } else {
                text = lead ? QString("The <b>lead sheet</b> changed%1. %2").arg(where, fileref(c.relativePath))
                       : QString("<b>%1</b>: your %2 part changed%3. %4").arg(esc(arr), esc(c.part), where, fileref(c.relativePath));
                addFor(who, text, "warn");
            }
        }
    }

    for (const auto& [pk, g] : chairGroups) {
        const SheetChange& c = g.change;
        const QString chairName = QRegularExpression("^Horn \\d+").match(c.part).captured(0);
        const QString sheets = QString("(your sheet%1: %2)").arg(g.files.size() > 1 ? "s" : "", esc(g.files.join(", ")));
        QString text;
        if (c.kind == SheetChange::Added) {
            text = QString("New: <b>%1</b>, %2 %3").arg(esc(g.arrangement), chairName, sheets);
            addFor({ pk.first }, text, "ok");
        } else if (!c.barsKnown) {
            text = QString("<b>%1</b>: %2 was updated %3").arg(esc(g.arrangement), chairName, sheets);
            addFor({ pk.first }, text, "warn");
        } else if (!c.bars.empty()) {
            text = QString("<b>%1</b>: %2 changed in bar%3 %4%5 %6")
                   .arg(esc(g.arrangement), chairName, c.bars.size() == 1 && c.bars[0].first == c.bars[0].second ? "" : "s", barRanges(c.bars),
                        c.letters.isEmpty() ? QString() : QString(" (letter%1 %2)").arg(c.letters.size() > 1 ? "s" : "", esc(c.letters.join(", "))),
                        sheets);
            addFor({ pk.first }, text, "warn");
        }
    }

    auto plain = [&](const QStringList& paths, const QString& kind) {
        for (const QString& abs : paths) {
            if (!abs.startsWith(prefix)) {
                continue;
            }
            const QString rel = abs.mid(prefix.size());
            if (described.count(rel)) {
                continue;
            }
            const QStringList who = readersOfSheet(rel, song.code, roster);
            if (who.isEmpty()) {
                continue;
            }
            const QString arr = folderLabel(rel);
            if (rel.startsWith("1 Lead Sheet/")) {
                addFor(who, kind == "added" ? QString("New <b>lead sheet</b>: %1").arg(fileref(rel))
                       : kind == "changed" ? QString("The <b>lead sheet</b> was updated: %1").arg(fileref(rel))
                       : QString("The <b>lead sheet</b> %1 was retired (it&rsquo;s kept in Version History).").arg(fileref(rel)),
                       kind == "changed" ? "warn" : kind == "added" ? "ok" : "");
                continue;
            }
            if (kind == "added") {
                addFor(who, QString("New sheet in <b>%1</b>: %2").arg(esc(arr), fileref(rel)), "ok");
            } else if (kind == "changed") {
                addFor(who, QString("<b>%1</b>: %2 was updated.").arg(esc(arr), fileref(rel)), "warn");
            } else {
                addFor(who, QString("<b>%1</b>: %2 was retired (it&rsquo;s kept in Version History).").arg(esc(arr), fileref(rel)), "");
            }
        }
    };
    plain(added, "added");
    plain(changed, "changed");
    plain(removed, "removed");

    const QString date = today.toString(Qt::ISODate);
    const QString title = exported && !exported->version.isEmpty() ? QString("version %1").arg(exported->version) : QString("sheets updated");
    QJsonObject songLog = changelog.value(song.root).toObject();
    int n = 0;
    for (auto& [player, list] : changes) {
        QJsonArray entries = songLog.value(player).toArray();
        QJsonObject top = entries.isEmpty() ? QJsonObject() : entries.first().toObject();
        if (top.value("date").toString() == date && top.value("title").toString() == title) {
            // a second export the same day adds to that day's entry
            QJsonArray c = top.value("changes").toArray();
            for (const QJsonValue& v : list) {
                if (!c.contains(v)) {
                    c.append(v);
                }
            }
            top["changes"] = c;
            entries[0] = top;
        } else {
            entries.insert(0, QJsonObject { { "date", date }, { "title", title }, { "changes", list } });
        }
        songLog[player] = entries;
        ++n;
    }
    if (n) {
        changelog[song.root] = songLog;
    }
    return n;
}

QStringList seedNewPlayers(QJsonObject& changelog, const Roster& roster)
{
    QStringList seeded;
    for (auto it = changelog.begin(); it != changelog.end(); ++it) {
        QJsonObject songLog = it.value().toObject();
        bool changed = false;
        for (const Player* p : roster.current()) {
            if (songLog.contains(p->name)) {
                continue;
            }
            for (const Player& former : roster.players) {
                if (former.current || !songLog.contains(former.name)) {
                    continue;
                }
                bool overlap = false;
                for (const QString& i : p->instruments) {
                    overlap |= former.instruments.contains(i);
                }
                if (overlap) {
                    songLog[p->name] = songLog.value(former.name);
                    changed = true;
                    if (!seeded.contains(p->name)) {
                        seeded << p->name;
                    }
                    break;
                }
            }
        }
        if (changed) {
            it.value() = songLog;
        }
    }
    return seeded;
}
}
