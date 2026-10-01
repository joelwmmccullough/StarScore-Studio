/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the organizer run
 */
#include "organizer.h"

#include <set>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QTime>

#include "orgfiling.h"
#include "orghtml.h"
#include "orglibrary.h"
#include "orgplatform.h"
#include "orgscan.h"
#include "orgstores.h"

namespace mu::project::starscore::org {
void onMainThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(fn), Qt::QueuedConnection);
}

void inBackground(std::function<void()> work, std::function<void()> then)
{
    QThread* t = QThread::create(std::move(work));
    QObject::connect(t, &QThread::finished, QCoreApplication::instance(), [t, then]() {
        t->deleteLater();
        then();
    }, Qt::QueuedConnection);
    t->start();
}

static const char* RULES_MD = R"MD(# Sheets and Demos: how it is kept in order

Since October 2026 StarScore Studio does all of this. It runs after every "Export to Sheets and Demos"
(unless "Run organization process" is unticked), and from the Dashboard's "Run Folder Organization" and
"Rebuild everything". Nothing runs on a schedule. The old Python toolkit is in `Deprecated/Retired <date>/`.

## Rules it keeps
- Nothing is ever deleted. A sheet that is replaced moves to the song's `Version History/Superseded <date>/`.
- Nothing inside `Version History`, `Old Versions` or `5 Archive` is ever renamed.
- Log entries (changelogs, the Maintenance Report) are only ever added on top.
- Every song folder has `1 Lead Sheet`, `1 Rhythm`, `Horn Part Guides`, `Demos`, `Version History` and one
  `Update Notes yy-mm-dd` folder, dated the last time the song's sheets changed. Horn folders appear only when
  there is a sheet for them.
- Folder and file names: see the Band Guide (`NH <instruments>`, `NH Any Horns`, `1H`, `CODE - Part.pdf`).

## What a run does
1. setlist.fm: asks about shows it hasn't seen once their date has passed; Joel pastes the YouTube link (or skips).
   Play counts are this year's plus last year's.
2. Files `6 Inbox` and loose sheets in song folders; registers numbered song folders that have no code.
3. Measures new or changed sheets (pages, music symbols, words) to find blank and short sheets.
4. Writes changelog entries: bar-by-bar after a StarScore export, file-level otherwise.
5. Rebuilds the PDFs that are out of date: What's Here, Recordings, changelogs, Horn Part Guides (from the score,
   for songs exported from StarScore), the Band Guide, the Progress tracker and the Maintenance Report;
   All Recordings and the Projects Maintenance Report in Projects and Sheets.
6. Projects and Sheets: files `9 Inbox` and loose top-level files into the right tune (older MuseScore material
   into the tune's `MuseScore Files/`), moves empty folders to `Z Empty Folders (safe to delete)`.
7. Colours the song folders in Finder (green / blue / yellow / red by Phase 1 progress), and each tune folder in
   Projects and Sheets the same colour as its song.

## Data files here
`codes.json`, `roster.json` (edit in StarScore: Dashboard > Band roster), `changelog.json`, `maintlog.json`,
`recordings.json`, `sheetcache.json`, `hornguides/CODE.json`, `organizer.json`, and `manifest.tsv` (the undo
record of the August 2026 migration).
)MD";

struct Organizer::Run {
    RunRequest request;
    Progress progress;
    std::function<void(RunSummary)> done;
    RunSummary summary;

    // stores
    Codes codes;
    Roster roster;
    bool rosterWasMissing = false;
    JsonStore recordings { "recordings.json" };
    JsonStore changelog { "changelog.json" };
    JsonStore maintlog { "maintlog.json" };
    JsonStore state { "organizer.json" };
    JsonStore projstate { "projstate.json" };
    SheetCache cache;

    // online
    OnlineResult online;
    std::vector<NewShow> offered;
    std::vector<ShowAnswer> answers;
    std::vector<ShowVideo> videos;
    QStringList recordingLog;
    QStringList recordingsChanged;   // codes
    bool playCountsChanged = false;

    // prepared
    struct Job {
        RenderJob render;
        QString finalPath;
        QString label;
        bool ok = false;
    };
    std::vector<Job> jobs;
    std::vector<std::pair<QString, QString> > colours;   // folder, colour
    QJsonObject bandEntry, projEntry;
    bool stoppedEarly = false;
    QString tempDir;
};

std::shared_ptr<Organizer> Organizer::create(const Paths& paths, Fetcher fetch, Prompts prompts)
{
    return std::shared_ptr<Organizer>(new Organizer(paths, std::move(fetch), std::move(prompts)));
}

Organizer::Organizer(const Paths& paths, Fetcher fetch, Prompts prompts)
    : m_paths(paths), m_fetch(std::move(fetch)), m_prompts(std::move(prompts))
{
}

void Organizer::stop()
{
    m_stop = true;
}

void Organizer::run(const RunRequest& request, const Progress& progress, std::function<void(RunSummary)> done)
{
    m_stop = false;
    auto r = std::make_shared<Run>();
    r->request = request;
    r->progress = progress;
    r->progress.stop = &m_stop;
    r->done = std::move(done);

    if (!QFileInfo(m_paths.band).isDir()) {
        r->summary.ok = false;
        r->summary.headline = "Sheets and Demos wasn't found, so nothing was organized.";
        r->done(r->summary);
        return;
    }
    r->codes.load(m_paths);
    r->rosterWasMissing = !QFileInfo::exists(m_paths.toolkit + "/roster.json");
    r->roster.load(m_paths);
    r->recordings.load(m_paths.toolkit);
    r->changelog.load(m_paths.toolkit);
    r->maintlog.load(m_paths.toolkit);
    r->state.load(m_paths.toolkit);
    if (!m_paths.projToolkit.isEmpty()) {
        r->projstate.load(m_paths.projToolkit);
    }
    if (r->recordings.doc.isNull()) {
        r->recordings.doc = QJsonDocument(QJsonObject { { "version", 1 } });
    }

    if (request.online && m_fetch) {
        r->progress.at("Looking for new shows on setlist.fm", 0.02);
        online(r);
    } else {
        prepare(r);
    }
}

// ------------------------------------------------------------------ 1. setlist.fm
void Organizer::online(std::shared_ptr<Run> r)
{
    auto self = shared_from_this();
    checkSetlistFm(r->recordings.doc.object(), m_paths.today, m_fetch, [self, r](OnlineResult res) {
        r->online = res;
        for (const QString& l : res.log) {
            r->progress.say(l);
        }
        QJsonObject rec = r->recordings.doc.object();
        if (res.reached) {
            if (!res.allShows.empty()) {
                baselineShows(rec, res.allShows, self->m_paths.today);
            }
            const QJsonObject before = rec.value("playCounts").toObject();
            storePlayCounts(rec, res, self->m_paths.today);
            QJsonObject after = rec.value("playCounts").toObject();
            after["fetched"] = before.value("fetched");
            r->playCountsChanged = after != before;
            r->recordings.doc = QJsonDocument(rec);
            r->recordings.changed = true;
        }
        if (res.newShows.empty() || !self->m_prompts.askShows || self->m_stop) {
            self->prepare(r);
            return;
        }
        r->offered = res.newShows;
        self->m_prompts.askShows(r->offered, [self, r](std::vector<ShowAnswer> answers) {
            r->answers = answers;
            std::vector<std::pair<NewShow, QString> > toRead;
            for (const ShowAnswer& a : answers) {
                if (a.kind == ShowAnswer::Link) {
                    for (const NewShow& ns : r->offered) {
                        if (ns.show.url == a.url) {
                            toRead.emplace_back(ns, a.link);
                        }
                    }
                }
            }
            auto apply = [self, r]() {
                QJsonObject rec = r->recordings.doc.object();
                r->recordingLog = applyShows(rec, r->offered, r->answers, r->videos, self->m_paths.today, r->recordingsChanged);
                r->recordings.doc = QJsonDocument(rec);
                r->recordings.changed = true;
                for (const QString& l : r->recordingLog) {
                    r->progress.say(QString(l).remove(QRegularExpression("<[^>]*>")).replace("&rsquo;", "’"));
                }
                self->prepare(r);
            };
            if (toRead.empty()) {
                apply();
                return;
            }
            r->progress.at("Reading the YouTube videos", 0.05);
            readVideos(toRead, r->recordings.doc.object(), self->m_fetch, [self, r, apply](std::vector<ShowVideo> videos) {
                r->videos = videos;
                bool anyUnsure = false;
                for (const ShowVideo& v : r->videos) {
                    for (const MatchedTimestamp& m : v.songs) {
                        anyUnsure |= m.code.isEmpty();
                    }
                }
                if (!self->m_prompts.confirmSongs || r->videos.empty()) {
                    apply();
                    return;
                }
                QStringList choices;
                for (const auto& [root, code] : r->codes.band) {
                    choices << code + " — " + (root.startsWith("4 Works In Progress/") ? root.mid(20) : root.mid(2));
                }
                Q_UNUSED(anyUnsure);
                self->m_prompts.confirmSongs(r->videos, choices, apply);
            });
        });
    });
}

// ------------------------------------------------------------------ 2. the work
//! As the old Colour Song Folders script: the lead sheet, the rhythm section and the 3-horn chart each score
//! 1 when done, 1/2 when started, 0 when missing. 3 is green, 2 or more blue, anything above 0 yellow, 0 red.
static QString colourFor(const SongInfo& s)
{
    int halves = 0;
    for (const QString& st : { s.status.lead, s.status.rhythm, s.status.three }) {
        halves += st == "done" ? 2 : st == "none" ? 0 : 1;
    }
    return halves >= 6 ? "Green" : halves >= 4 ? "Blue" : halves > 0 ? "Yellow" : "Red";
}

static QString movesHtml(const std::vector<Move>& moves, int max = 8)
{
    QStringList out;
    for (size_t i = 0; i < moves.size() && int(i) < max; ++i) {
        out << QString("<span class=\"mono\">%1</span> &rarr; <span class=\"mono\">%2</span>").arg(esc(moves[i].from), esc(moves[i].to));
    }
    if (int(moves.size()) > max) {
        out << QString("and %1 more").arg(moves.size() - max);
    }
    return out.join("; ");
}

static QJsonObject action(const QString& head, const QString& kind, const QString& text)
{
    return QJsonObject { { "head", head }, { "kind", kind }, { "text", text } };
}

void Organizer::prepare(std::shared_ptr<Run> r)
{
    if (m_stop) {
        r->summary.stopped = true;
        finish(r);
        return;
    }
    auto self = shared_from_this();
    // progress messages from the worker go through the main thread
    Progress bg = r->progress;
    bg.log = [p = r->progress](const QString& l) { onMainThread([p, l]() { p.say(l); }); };
    bg.step = [p = r->progress](const QString& s, double f) { onMainThread([p, s, f]() { p.at(s, f); }); };

    inBackground([self, r, bg]() {
        const Paths& paths = self->m_paths;
        QJsonArray bandActions, projActions;
        QStringList plainLines, warnings;
        QJsonObject rec = r->recordings.doc.object();
        QJsonObject orgState = r->state.doc.object();
        const bool firstRun = orgState.isEmpty();

        // the sheet measurements (the first run takes the old toolkit's, before that toolkit is retired)
        r->cache.load(paths);
        const bool cacheWasEmpty = r->cache.isEmpty();

        // --- first run: retire the Python toolkits, write the new rule book
        if (firstRun) {
            bg.at("Retiring the old Python toolkit", 0.08);
            const QStringList retired = retireOldToolkits(paths);
            writeText(paths.toolkit + "/RULES.md", RULES_MD);
            if (!retired.isEmpty()) {
                bandActions.append(action("Retired", "change",
                                          QString("StarScore Studio now does the organizing. The old toolkit (%1 files and folders, "
                                                  "including <span class=\"mono\">organize.py</span> and the generators) moved to "
                                                  "<span class=\"mono\">6 Inbox/.organizer/Deprecated/Retired %2/</span>; "
                                                  "<span class=\"mono\">RULES.md</span> was rewritten for the new way.")
                                          .arg(retired.size()).arg(paths.todayIso())));
                plainLines << QString("Retired the old Python toolkit (%1 items moved to Deprecated).").arg(retired.size());
            }
        }

        // --- the export's own recordings copy
        if (r->request.exported && !r->request.exported->recordings.isEmpty()) {
            if (mergeSongRecordings(rec, r->request.exported->code, r->request.exported->recordings)) {
                r->recordingsChanged << r->request.exported->code;
                r->recordings.changed = true;
            }
        }
        if (!r->recordingLog.isEmpty()) {
            bandActions.append(action("Recordings", "change", r->recordingLog.join("<br>")));
        }
        if (r->online.reached && r->playCountsChanged) {
            bandActions.append(action("Play counts", "", QString("Read from setlist.fm: %1 and %2 combined.")
                                      .arg(paths.today.year() - 1).arg(paths.today.year())));
        }

        // --- filing
        bg.at("Filing Sheets and Demos", 0.12);
        QJsonObject aliases = rec.value("aliases").toObject();
        BandFilingReport filing = fileBand(paths, r->codes, r->roster, aliases, bg);
        if (aliases != rec.value("aliases").toObject()) {
            rec["aliases"] = aliases;
            r->recordings.changed = true;
        }
        if (!filing.registered.isEmpty()) {
            bandActions.append(action("Registered", "change", "New song folders given codes: " + esc(filing.registered.join(", ")) + "."));
            plainLines << "Registered: " + filing.registered.join(", ");
        }
        if (!filing.inboxFiled.empty()) {
            bandActions.append(action("Inbox", "change", QString("%1 filed: %2.").arg(plural(int(filing.inboxFiled.size()), "file"),
                                                                                       movesHtml(filing.inboxFiled))));
            plainLines << QString("Filed %1 from 6 Inbox.").arg(plural(int(filing.inboxFiled.size()), "file"));
        }
        if (!filing.filed.empty()) {
            bandActions.append(action("Filed", "change", QString("%1 sitting loose in song folders: %2.")
                                      .arg(plural(int(filing.filed.size()), "file"), movesHtml(filing.filed))));
            plainLines << QString("Filed %1 found loose in song folders.").arg(plural(int(filing.filed.size()), "file"));
        }
        if (!filing.superseded.empty()) {
            bandActions.append(action("Archived", "", QString("%1 replaced by a newer one moved to Version History.")
                                      .arg(plural(int(filing.superseded.size()), "sheet"))));
        }
        QStringList needsLook = filing.unrecognised + filing.inboxUnmatched;
        if (!needsLook.isEmpty()) {
            QStringList esced;
            for (const QString& s : needsLook) {
                esced << "<span class=\"mono\">" + esc(s) + "</span>";
            }
            bandActions.append(action("Needs a look", "warn", "Couldn't tell where these go, so they were left where they are: "
                                      + esced.join(", ") + "."));
            for (const QString& s : needsLook) {
                warnings << "Couldn't file: " + s;
            }
        }
        if (!filing.newSongs.isEmpty()) {
            bandActions.append(action("Unnumbered folders", "warn", "Top-level folders with no number in front: <b>" + esc(filing.newSongs.join(", "))
                                      + "</b>. Give each a 1, 2, 3 (or move it into 4 Works In Progress) and it will be registered."));
            for (const QString& s : filing.newSongs) {
                warnings << "Top-level folder without a number: " + s;
            }
        }

        // --- codes in Projects and Sheets
        const QStringList codeLog = syncCodes(paths, r->codes);
        if (!codeLog.isEmpty()) {
            projActions.append(action("Codes", "change", codeLog.join("<br>")));
        }

        if (self->m_stop) {
            r->stoppedEarly = true;
            r->recordings.doc = QJsonDocument(rec);
            return;
        }

        // --- measuring
        bg.at("Reading new and changed sheets", 0.2);
        ScanResult scan = scanBand(paths, r->cache, bg);
        if (self->m_stop) {
            r->stoppedEarly = true;
            r->recordings.doc = QJsonDocument(rec);
            return;
        }
        QStringList changedRoots;
        for (const QStringList* l : { &scan.added, &scan.changed, &scan.removed }) {
            for (const QString& p : *l) {
                const QString root = songRootOf(p);
                if (!root.isEmpty() && !changedRoots.contains(root)) {
                    changedRoots << root;
                }
            }
        }
        // the exported song counts as changed only when the export wrote something (a re-export where every sheet
        // came out the same leaves its files, and its Update Notes date, as they were)
        if (r->request.exported && !r->request.exported->written.isEmpty() && !changedRoots.contains(r->request.exported->songRoot)) {
            changedRoots << r->request.exported->songRoot;
        }
        redateUpdateNotes(paths, r->codes, changedRoots, filing);
        if (!filing.updateNotesRedated.isEmpty()) {
            bandActions.append(action("Update Notes", "", "Re-dated: " + esc(filing.updateNotesRedated.join("; ")) + "."));
        }
        if (!filing.foldersMade.isEmpty()) {
            QStringList made = filing.foldersMade.mid(0, 12);
            if (filing.foldersMade.size() > 12) {
                made << QString("and %1 more").arg(filing.foldersMade.size() - 12);
            }
            bandActions.append(action("Folders", "", "Made: <span class=\"mono\">" + esc(made.join(", ")) + "</span>."));
        }
        if (scan.measuredNow) {
            plainLines << QString("Measured %1.").arg(plural(scan.measuredNow, "new or changed sheet"));
        }

        // --- horn analysis from the export
        if (r->request.exported && !r->request.exported->hornAnalysis.isEmpty()) {
            writeJson(paths.toolkit + "/hornguides/" + r->request.exported->code + ".json", QJsonDocument(r->request.exported->hornAnalysis));
        }

        // --- the library
        Library lib = buildLibrary(scan, r->codes.band);
        for (SongInfo& s : lib.songs) {
            const QStringList un = QDir(paths.band + "/" + s.root).entryList({ "Update Notes*" }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            s.updateNotes = un.isEmpty() ? QString() : un.last();
        }

        // --- changelogs
        QJsonObject changelog = r->changelog.doc.object();
        const QStringList seeded = seedNewPlayers(changelog, r->roster);
        if (!seeded.isEmpty()) {
            bandActions.append(action("Changelogs", "", esc(seeded.join(", ")) + " now carr" + (seeded.size() == 1 ? "ies" : "y")
                                      + " the change history of the former player on the same instrument."));
        }
        int entries = 0;
        if (!cacheWasEmpty || r->request.exported) {
            for (const QString& root : changedRoots) {
                auto it = lib.byRoot.find(root);
                if (it == lib.byRoot.end()) {
                    continue;
                }
                const ExportInfo* ex = r->request.exported && r->request.exported->songRoot == root ? &*r->request.exported : nullptr;
                entries += addChangelogEntries(changelog, lib.songs[it->second], r->roster, ex, scan.added, scan.changed, scan.removed,
                                               paths.today);
            }
        }
        if (entries) {
            bandActions.append(action("Changelogs", "change", QString("%1 added for the players whose sheets changed.").arg(plural(entries, "entry", "entries"))));
            plainLines << QString("Wrote %1.").arg(plural(entries, "changelog entry", "changelog entries"));
            r->changelog.doc = QJsonDocument(changelog);
            r->changelog.changed = true;
        } else if (!seeded.isEmpty()) {
            r->changelog.doc = QJsonDocument(changelog);
            r->changelog.changed = true;
        }

        // --- what to rebuild
        const QString rosterHash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject { { "p", [&]() {
            QJsonArray a;
            for (const Player& p : r->roster.players) {
                a.append(p.toJson());
            }
            return a;
        }() } }).toJson(QJsonDocument::Compact), QCryptographicHash::Md5).toHex());
        const bool everything = r->request.scope == RunRequest::RebuildAll || firstRun
                                || orgState.value("rosterHash").toString() != rosterHash;
        std::set<QString> songsToBuild;
        for (const SongInfo& s : lib.songs) {
            if (s.code == "????") {
                continue;
            }
            const QDir dir(paths.band + "/" + s.root);
            bool missing = !dir.exists(s.code + " - What's Here.pdf") || !dir.exists(s.code + " - Recordings.pdf");
            for (const Player* p : r->roster.current()) {
                missing |= !s.updateNotes.isEmpty() && !dir.exists(s.updateNotes + "/" + s.code + " - Changelog - " + p->name + ".pdf");
            }
            // after an export the song's pages are rebuilt even when no sheet changed: its horn guides come from that export
            const bool exportedSong = r->request.exported && r->request.exported->songRoot == s.root;
            if (everything || missing || exportedSong || changedRoots.contains(s.root) || r->recordingsChanged.contains(s.code)) {
                songsToBuild.insert(s.root);
            }
        }

        // --- the HTML
        r->tempDir = QDir::tempPath() + "/StarScore Organizer " + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QDir().mkpath(r->tempDir);
        auto job = [&](const QString& html, const QString& finalPath, const QString& label, const QString& margins) {
            Run::Job j;
            j.render.html = html;
            j.render.pdfPath = QString("%1/%2.pdf").arg(r->tempDir).arg(r->jobs.size(), 4, 10, QChar('0'));
            const QStringList mm = margins.split(' ');
            auto pt = [](const QString& v) { return v.chopped(2).toDouble() * 72.0 / 25.4; };
            j.render.marginTop = pt(mm.value(0));
            j.render.marginRight = pt(mm.value(1));
            j.render.marginBottom = pt(mm.value(2));
            j.render.marginLeft = pt(mm.value(3));
            j.finalPath = finalPath;
            j.label = label;
            r->jobs.push_back(j);
        };
        const QString BASE_M = "14mm 15mm 13mm 15mm", REC_M = "12mm 13mm 12mm 13mm";
        RecordingsData recData;
        recData.load(rec);
        QJsonArray formerMoves;
        int hornGuideFiles = 0, changelogFiles = 0;
        int songIndex = 0;
        for (const SongInfo& s : lib.songs) {
            hornGuideFiles += int(s.hornGuides.size());
            changelogFiles += int(r->roster.current().size());
            if (!songsToBuild.count(s.root)) {
                continue;
            }
            bg.at(QString("Writing %1").arg(s.name), 0.3 + 0.3 * (++songIndex) / std::max<size_t>(1, songsToBuild.size()));
            const QString dir = paths.band + "/" + s.root;
            job(whatsHereHtml(s, r->roster, paths.today), dir + "/" + s.code + " - What's Here.pdf", s.code + " What's Here", BASE_M);
            job(recordingsHtml(recData, s, paths.today), dir + "/" + s.code + " - Recordings.pdf", s.code + " Recordings", REC_M);
            if (!s.updateNotes.isEmpty()) {
                const QJsonObject songLog = changelog.value(s.root).toObject();
                for (const Player* p : r->roster.current()) {
                    job(changelogHtml(s, *p, songLog.value(p->name).toArray()),
                        dir + "/" + s.updateNotes + "/" + s.code + " - Changelog - " + p->name + ".pdf", s.code + " changelog " + p->name, BASE_M);
                }
                // former members' changelogs leave Update Notes for Version History
                for (const Player& p : r->roster.players) {
                    const QString f = dir + "/" + s.updateNotes + "/" + s.code + " - Changelog - " + p.name + ".pdf";
                    if (!p.current && QFileInfo::exists(f)) {
                        const QString to = freeName(dir + "/Version History/Superseded " + paths.todayIso() + "/" + s.updateNotes + "/"
                                                    + QFileInfo(f).fileName());
                        if (moveItem(f, to)) {
                            formerMoves.append(relativeTo(paths.band, to));
                        }
                    }
                }
            }
            const QJsonObject analysis = readJsonObject(paths.toolkit + "/hornguides/" + s.code + ".json");
            if (!analysis.isEmpty()) {
                for (const Player* p : r->roster.currentHorns()) {
                    job(hornGuideHtml(s, *p, analysis, r->roster), dir + "/Horn Part Guides/" + s.code + " - " + p->name + ".pdf",
                        s.code + " horn guide " + p->name, BASE_M);
                }
            }
        }
        if (!formerMoves.isEmpty()) {
            bandActions.append(action("Former players", "", QString("%1 of former band members moved from Update Notes to Version History.")
                                      .arg(plural(formerMoves.size(), "changelog"))));
        }

        // --- Projects and Sheets
        if (!paths.projects.isEmpty() && QFileInfo(paths.projects).isDir()) {
            bg.at("Filing Projects and Sheets", 0.65);
            const ProjectsFilingReport pf = fileProjects(paths, bg);
            if (!pf.filed.empty()) {
                projActions.append(action("Filed", "change", QString("%1: %2.").arg(plural(int(pf.filed.size()), "file"), movesHtml(pf.filed))));
                plainLines << QString("Filed %1 in Projects and Sheets.").arg(plural(int(pf.filed.size()), "file"));
            }
            if (!pf.superseded.empty()) {
                projActions.append(action("Superseded", "", movesHtml(pf.superseded)));
            }
            if (!pf.swept.empty()) {
                projActions.append(action("Empty folders", "", QString("%1 moved to <span class=\"mono\">Z Empty Folders (safe to delete)</span>.")
                                          .arg(plural(int(pf.swept.size()), "folder"))));
            }
            if (!pf.unmatched.isEmpty()) {
                QStringList e;
                for (const QString& u : pf.unmatched) {
                    e << "<span class=\"mono\">" + esc(u) + "</span>";
                    warnings << "Projects and Sheets: couldn't file " + u;
                }
                projActions.append(action("Needs a look", "warn", "No tune folder matches these, so they stay where they are: " + e.join(", ") + "."));
            }
            if (firstRun) {
                projActions.append(action("Retired", "change", "The two-way sync of the split MuseScore files and the rest of the Python "
                                          "toolkit moved to <span class=\"mono\">.organizer/Deprecated/</span>. Each tune&rsquo;s "
                                          "<span class=\"mono\">.starscore</span> is the live file; StarScore Studio keeps this folder in order."));
            }
        }

        // --- log entries (added before the reports are written, so they show this run)
        const QString title = r->request.exported ? QString("export: %1 %2").arg(r->request.exported->title, r->request.exported->version)
                              : r->request.scope == RunRequest::RebuildAll ? QString("rebuilt everything") : QString("folder organization");
        int songPdfs = int(r->jobs.size());
        const bool topLevel = songPdfs > 0 || bandActions.size() > 0 || everything;
        if (r->request.exported) {
            const ExportInfo& ex = *r->request.exported;
            bandActions.insert(0, action("Exported", "change", QString("<b>%1</b> version %2 from StarScore Studio: %3 written%4.")
                                         .arg(esc(ex.title), esc(ex.version), plural(int(ex.written.size()), "sheet"),
                                              ex.archived.isEmpty() ? QString()
                                              : QString(", %1 moved to Version History").arg(plural(int(ex.archived.size()), "older sheet")))));
        }
        if (topLevel) {
            bandActions.append(action("Regenerated", "", QString("%1 for %2, plus the Band Guide, the Progress tracker and this report.")
                                      .arg(plural(songPdfs, "PDF"), plural(int(songsToBuild.size()), "song"))));
        }
        QJsonArray mlog = r->maintlog.doc.array();
        if (!bandActions.isEmpty()) {
            r->bandEntry = QJsonObject { { "date", paths.todayIso() }, { "title", title }, { "actions", bandActions },
                                         { "time", QTime::currentTime().toString("HH:mm") } };
            mlog.insert(0, r->bandEntry);
            r->maintlog.doc = QJsonDocument(mlog);
            r->maintlog.changed = true;
        }

        // --- play counts for the tracker
        PlayCounts plays;
        plays.year = paths.today.year();
        const QJsonObject pc = rec.value("playCounts").toObject();
        auto counts = [&](int year, std::map<QString, int>& out) {
            QMap<QString, int> byName;
            const QJsonObject o = pc.value(QString::number(year)).toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                byName[it.key()] = it.value().toInt();
            }
            mapPlayCounts(rec, byName, out, plays.notInLibrary);
        };
        counts(plays.year, plays.thisYear);
        counts(plays.year - 1, plays.lastYear);
        for (const QJsonValue& v : rec.value("skipNames").toArray()) {
            plays.notInLibrary.removeAll(v.toString());
        }

        if (topLevel) {
            job(bandGuideHtml(lib, paths.today), paths.band + "/Starsign Band Guide.pdf", "Band Guide", BASE_M);
            job(progressHtml(lib, plays, paths.today), paths.band + "/Starsign Progress - Joel.pdf", "Progress tracker", BASE_M);
            job(maintenanceHtml(lib, plays, mlog, paths.today, hornGuideFiles, changelogFiles), paths.band + "/Starsign Maintenance Report.pdf",
                "Maintenance Report", BASE_M);
        }
        if (!paths.projects.isEmpty() && QFileInfo(paths.projects).isDir()) {
            const bool recordingsMoved = !r->recordingsChanged.isEmpty() || everything
                                         || !QFileInfo::exists(paths.projects + "/All Recordings.pdf");
            if (recordingsMoved) {
                job(allRecordingsHtml(recData, lib, paths.today), paths.projects + "/All Recordings.pdf", "All Recordings", REC_M);
                projActions.append(action("All Recordings", "", "Rebuilt from the latest recordings."));
            }
            QJsonObject ps = r->projstate.doc.object();
            const QJsonObject snap = projectsSnapshot(paths, r->codes);
            QJsonArray plog = ps.value("log").toArray();
            const bool countsMoved = snap.value("files") != ps.value("files") || snap.value("tunes") != ps.value("tunes");
            if (projActions.size() > (recordingsMoved ? 1 : 0)) {
                r->projEntry = QJsonObject { { "date", paths.todayIso() }, { "title", title }, { "actions", projActions } };
                plog.insert(0, r->projEntry);
            }
            for (auto it = snap.begin(); it != snap.end(); ++it) {
                ps[it.key()] = it.value();
            }
            ps["log"] = plog;
            if (ps != r->projstate.doc.object() || everything) {
                r->projstate.doc = QJsonDocument(ps);
                r->projstate.changed = true;
                job(projectsReportHtml(ps, paths.today), paths.projects + "/Projects Maintenance Report.pdf", "Projects Maintenance Report", BASE_M);
            }
            Q_UNUSED(countsMoved);
        }

        // --- folder colours: the band's song folders, and each tune folder in Projects and Sheets in its song's colour
        //     (matched by code; tunes with no song in Sheets and Demos keep whatever colour they have)
        std::map<QString, QString> colourOfCode;
        for (const SongInfo& s : lib.songs) {
            if (s.code != "????") {
                const QString c = colourFor(s);
                r->colours.emplace_back(paths.band + "/" + s.root, c);
                colourOfCode[s.code] = c;
            }
        }
        if (!paths.projects.isEmpty() && QFileInfo(paths.projects).isDir()) {
            // only the Starsign groups (1 Originals, 2 Covers, 3 WIP): "4 Other Projects" can hold a tune of the same name
            std::vector<std::pair<QString, QString> > starsignTunes;
            const QDir base(paths.projects);
            for (const QString& group : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (!QRegularExpression("^[123] ").match(group).hasMatch()) {
                    continue;
                }
                for (const QFileInfo& t : QDir(base.filePath(group)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                    if (!t.isSymLink() && !t.fileName().startsWith('.')) {
                        starsignTunes.emplace_back(t.fileName(), group + "/" + t.fileName());
                    }
                }
            }
            for (const auto& [name, rel] : starsignTunes) {
                QString code;
                const auto known = r->codes.projects.find(name);
                if (known != r->codes.projects.end()) {
                    code = known->second;
                } else {
                    for (const QString& f : QDir(paths.projects + "/" + rel).entryList({ "*.starscore" }, QDir::Files, QDir::Name)) {
                        code = QRegularExpression("^([A-Z]{4}) - ").match(f).captured(1);
                        if (!code.isEmpty()) {
                            break;
                        }
                    }
                }
                const auto c = colourOfCode.find(code);
                if (!code.isEmpty() && c != colourOfCode.end()) {
                    r->colours.emplace_back(paths.projects + "/" + rel, c->second);
                }
            }
        }

        orgState["version"] = 1;
        if (firstRun) {
            orgState["firstRun"] = paths.todayIso();
        }
        orgState["lastRun"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        orgState["rosterHash"] = rosterHash;
        r->state.doc = QJsonDocument(orgState);
        r->state.changed = true;
        r->recordings.doc = QJsonDocument(rec);

        r->summary.lines = plainLines;
        r->summary.warnings = warnings;
        r->summary.changedCodes = r->recordingsChanged;
    }, [self, r]() {
        if (r->stoppedEarly || self->m_stop) {
            r->summary.stopped = true;
            // keep what was filed and registered; nothing else is half-saved
            r->codes.saveBand(self->m_paths);
            r->recordings.save(self->m_paths.toolkit);
            self->finish(r);
            return;
        }
        self->render(r);
    });
}

// ------------------------------------------------------------------ 3. printing
void Organizer::render(std::shared_ptr<Run> r)
{
    auto self = shared_from_this();
    if (r->jobs.empty()) {
        deploy(r);
        return;
    }
    if (!m_htmlInstead.isEmpty() || !canRenderPdf()) {
        for (Run::Job& j : r->jobs) {
            j.ok = writeText(j.render.pdfPath, j.render.html.toUtf8());
            if (!m_htmlInstead.isEmpty()) {
                writeText(m_htmlInstead + "/" + relativeTo(QFileInfo(m_paths.band).absolutePath(), j.finalPath) + ".html",
                          j.render.html.toUtf8());
            }
        }
        deploy(r);
        return;
    }
    std::vector<RenderJob> jobs;
    for (const Run::Job& j : r->jobs) {
        jobs.push_back(j.render);
    }
    const int total = int(jobs.size());
    r->progress.at(QString("Making %1").arg(plural(total, "PDF")), 0.7);
    renderPdfs(jobs, [r, total](int i, bool ok) {
        r->jobs[i].ok = ok;
        if (!ok) {
            r->progress.say("Couldn't make " + r->jobs[i].label);
        }
        r->progress.at(QString("Making PDFs (%1 of %2)").arg(i + 1).arg(total), 0.7 + 0.25 * (i + 1) / total);
    }, [self, r]() { self->deploy(r); });
}

// ------------------------------------------------------------------ 4. putting things in place
void Organizer::deploy(std::shared_ptr<Run> r)
{
    auto self = shared_from_this();
    r->progress.at("Putting the PDFs in place", 0.96);
    inBackground([self, r]() {
        for (Run::Job& j : r->jobs) {
            if (!j.ok) {
                ++r->summary.pdfsFailed;
                continue;
            }
            QFile f(j.render.pdfPath);
            if (f.open(QIODevice::ReadOnly) && writeText(j.finalPath, f.readAll())) {
                ++r->summary.pdfsWritten;
            } else {
                ++r->summary.pdfsFailed;
            }
        }
        const Paths& paths = self->m_paths;
        r->cache.save(paths);
        r->codes.saveBand(paths);
        if (r->codes.projectsChanged) {
            r->codes.saveProjects(paths);
        }
        if (r->rosterWasMissing && !r->roster.players.empty()) {
            r->roster.save(paths);
        }
        r->recordings.save(paths.toolkit);
        r->changelog.save(paths.toolkit);
        r->maintlog.save(paths.toolkit);
        r->state.save(paths.toolkit);
        if (!paths.projToolkit.isEmpty()) {
            r->projstate.save(paths.projToolkit);
        }
        QDir(r->tempDir).removeRecursively();   // our own temporary copies
    }, [self, r]() {
        int recoloured = 0;
        for (const auto& [folder, colour] : r->colours) {
            if (finderColour(folder) != colour && setFinderColour(folder, colour)) {
                ++recoloured;
            }
        }
        if (recoloured) {
            r->summary.lines << QString("Recoloured %1 in Finder.").arg(plural(recoloured, "folder"));
        }
        self->finish(r);
    });
}

void Organizer::finish(std::shared_ptr<Run> r)
{
    RunSummary& s = r->summary;
    if (s.stopped) {
        s.headline = "Stopped. Files already filed stay filed; everything else is left for the next run.";
    } else {
        QStringList bits;
        if (s.pdfsWritten) {
            bits << plural(s.pdfsWritten, "PDF") + " rebuilt";
        }
        if (s.pdfsFailed) {
            bits << plural(s.pdfsFailed, "PDF") + " couldn't be made";
            s.ok = false;
        }
        s.headline = bits.isEmpty() ? QString("Everything was already up to date.") : "Done: " + bits.join(", ") + ".";
    }
    r->progress.at(s.headline, 1.0);
    r->done(s);
}
}
