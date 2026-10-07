/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — automatic test run, for checking changes on a Linux build before a Mac build is made.
 *
 * Start StarScore with a .starscore file and the environment variable STARSCORE_AUTOTEST set to an output folder:
 *
 *   QT_QPA_PLATFORM=offscreen STARSCORE_AUTOTEST=/tmp/out STARSCORE_AUTOTEST_STEPS=report,view,report,export \
 *     STARSCORE_AUTOTEST_BAND="/path/Sheets and Demos" ./StarScore song.starscore
 *
 * Once the song has opened (and the tidying on opening has run), the steps run in order, then StarScore quits.
 * Steps:
 *   report        report-N.json: parts, sections, every part score's title frame (texts, offsets, page rectangles,
 *                 whether the composer credit meets the arrangement label), barlines that differ between staves;
 *                 and parts-N/<part score>.pdf, each part score as StarScore shows it
 *   view          shows every part score in turn (what opening a part score does)
 *   bassversions  makes the 7-Horn section's main low horn's missing stand-in versions (no questions asked);
 *   lowversions   the same name for the same step, for a section whose main low horn isn't the bass trombone
 *   section:KEY[:DOUBLER[:LOW]]   adds a section from a template (section:2-horn-any is a 2-Horn Flexible section),
 *                 with the New StarScore doubler / 7th-horn instrument ids (section:7-horn:bb-clarinet:tuba)
 *   new:KEY[:DOUBLER[:LOW]]   makes a new StarScore from an arrangement template, replacing the open song, and logs
 *                 its parts and sections (new:7-horn-standard:bb-clarinet:contrabass-clarinet)
 *   styles        applies the part styles to everything
 *   version:X     sets the version number (version:1.2.3) in the song and every part score's footer
 *   export        Export to Sheets and Demos into STARSCORE_AUTOTEST_BAND (a copy, never the real folder)
 *   chords        the chord charts' three outputs: chords.json (the lead sheet's form and chords, extract.py's
 *                 JSON), chart.html (the PDF page, without the version footer, as pdfchart.py writes it),
 *                 ireal.html (the iReal Pro page); plus CODE - Chord Chart.html with the footer. The page's font
 *                 URLs come from STARSCORE_AUTOTEST_FONTS ("modernoir;jost;bravura"), else the app's embedded ones.
 *   standardize   makes the song standard (standardize:plan only lists what it would change), then lists its
 *                 sections, arrangements and part scores
 *   timevariants:half|double|both   the Half-Time / Double-Time sheets (every sheet) into STARSCORE_AUTOTEST_BAND
 *   todouble      the open song rewritten at twice its note lengths
 *   save          saves a copy of the song as saved.starscore
 * Everything is logged to log.txt.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"
#include "starscorechordchart.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "settings.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/dom/select.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/text.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/box.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/style/style.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
QString autotestDir()
{
    return qEnvironmentVariable("STARSCORE_AUTOTEST");
}

void autotestLog(const QString& line)
{
    QFile f(autotestDir() + "/log.txt");
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write((line + "\n").toUtf8());
    }
}

QJsonObject rectJson(const mu::engraving::RectF& r)
{
    return QJsonObject { { "x", r.x() }, { "y", r.y() }, { "w", r.width() }, { "h", r.height() } };
}

QString safeName(QString s)
{
    static const QRegularExpression unsafeRe("[/:\\\\]");
    s.replace(unsafeRe, "-");
    return s.trimmed();
}
}

bool StarScoreService::autotestRequested()
{
    return !autotestDir().isEmpty();
}

void StarScoreService::startAutotest()
{
    if (m_autotestStarted || !autotestRequested()) {
        return;
    }
    m_autotestStarted = true;
    QDir().mkpath(autotestDir());
#ifdef STARSCORE_VERSION_STR
    const QString version = QString::fromUtf8(STARSCORE_VERSION_STR);
#else
    const QString version = QString("?");
#endif
    autotestLog(QString("autotest: StarScore %1, steps %2").arg(version, qEnvironmentVariable("STARSCORE_AUTOTEST_STEPS")));
    QStringList steps = qEnvironmentVariable("STARSCORE_AUTOTEST_STEPS", "report").split(',', Qt::SkipEmptyParts);
    // after the tidying on opening (a zero timer) and the window settling
    QTimer::singleShot(3000, &m_timerGuard, [this, steps]() { runAutotestSteps(steps, 1); });
}

void StarScoreService::runAutotestSteps(QStringList steps, int reportNumber)
{
    if (steps.isEmpty()) {
        autotestLog("autotest: done");
        std::fflush(nullptr);
        std::_Exit(0);   // no "save changes?" question, no shutdown work
    }
    const QString step = steps.takeFirst().trimmed();
    // waitupdate: until "Update all sheets" has finished (it opens and closes songs, so no score check here)
    if (step == "waitupdate") {
        const StarScoreUpdateAllStatus st = updateAllStatus();
        if (st.running) {
            steps.prepend(step);
            QTimer::singleShot(2000, &m_timerGuard, [this, steps, reportNumber]() { runAutotestSteps(steps, reportNumber); });
            return;
        }
        autotestLog("step: waitupdate");
        autotestLog("  " + st.results.join("\n  "));
        QTimer::singleShot(500, &m_timerGuard, [this, steps, reportNumber]() { runAutotestSteps(steps, reportNumber); });
        return;
    }
    autotestLog("step: " + step);
    QElapsedTimer stepClock;   // how long the step took, in the log (for timing changes)
    stepClock.start();
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        autotestLog("  no score open");
        std::_Exit(2);
    }

    if (step == "report") {
        autotestReport(QString("%1/report-%2").arg(autotestDir()).arg(reportNumber));
        ++reportNumber;
    } else if (step == "view") {
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (INotationPtr n = e->notation()) {
                master->setExcerptIsOpen(n, true);
                globalContext()->setCurrentNotation(n);
                QCoreApplication::processEvents();
                clearComposerInCurrentScore();
            }
        }
        globalContext()->setCurrentNotation(master->notation());
        autotestLog(QString("  viewed %1 part scores").arg(master->excerpts().size()));
    } else if (step.startsWith("shotdialog:")) {
        // shotdialog:NAME: a picture of each open window other than the main one, as NAME-1.png, NAME-2.png…
        int k = 0;
        for (QWindow* w : QGuiApplication::topLevelWindows()) {
            auto* q = qobject_cast<QQuickWindow*>(w);
            if (q && q->isVisible() && q->objectName() != "ApplicationWindow") {
                const QString path = QString("%1/%2-%3.png").arg(autotestDir(), step.mid(11)).arg(++k);
                q->grabWindow().save(path);
                autotestLog(QString("  %1: %2 x %3").arg(path).arg(q->width()).arg(q->height()));
            }
        }
    } else if (step.startsWith("window:") || step.startsWith("shot:")) {
        // the app window: "window:W:H" resizes it, "shot:NAME" saves a picture of it as NAME.png
        QQuickWindow* win = nullptr;
        for (QWindow* w : QGuiApplication::topLevelWindows()) {
            auto* q = qobject_cast<QQuickWindow*>(w);
            if (q && q->objectName() == "ApplicationWindow") {
                win = q;
                break;
            }
            if (q && q->isVisible() && (!win || q->width() * q->height() > win->width() * win->height())) {
                win = q;
            }
        }
        if (!win) {
            autotestLog("  no window");
        } else if (step.startsWith("window:")) {
            // "window:W:H" or "window:W:H:STEP" (from the current width to W in steps, like dragging the edge)
            const QStringList wh = step.mid(7).split(':');
            const int target = wh.value(0).toInt();
            const int stride = std::max(1, wh.value(2).toInt());
            if (wh.size() > 2) {
                int w = win->width();
                while (w != target) {
                    w = w > target ? std::max(target, w - stride) : std::min(target, w + stride);
                    win->resize(w, wh.value(1).toInt());
                    for (int i = 0; i < 6; ++i) {
                        QCoreApplication::processEvents();
                    }
                }
            }
            win->resize(target, wh.value(1).toInt());
            for (int i = 0; i < 20; ++i) {
                QCoreApplication::processEvents();
            }
            autotestLog(QString("  window %1 x %2 (minimum %3 x %4)").arg(win->width()).arg(win->height())
                        .arg(win->minimumWidth()).arg(win->minimumHeight()));
        } else {
            const QImage img = win->grabWindow();
            const bool ok = img.save(autotestDir() + "/" + step.mid(5) + ".png");
            autotestLog(QString("  shot %1 x %2 %3").arg(img.width()).arg(img.height()).arg(ok ? "saved" : "not saved"));
        }
    } else if (step == "viewtabs") {
        // as the app does it: only switching tabs, the service's own reaction places the credit
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (INotationPtr n = e->notation()) {
                master->setExcerptIsOpen(n, true);
                globalContext()->setCurrentNotation(n);
                for (int i = 0; i < 5; ++i) {
                    QCoreApplication::processEvents();
                }
            }
        }
        globalContext()->setCurrentNotation(master->notation());
        autotestLog(QString("  switched to %1 part scores").arg(master->excerpts().size()));
    } else if (step == "bassversions" || step == "lowversions" || step == "versions") {
        // Every part with stand-in versions (a 7-Horn section's main low horn, a piccolo) gets the versions it lacks, as
        // "Make the other versions…" does without asking
        for (const StarScoreSection& s : load().sections) {
            for (const auto& [pid, mainName] : versionMains(s.id)) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (!p) {
                    continue;
                }
                autotestLog(QString("  main part: %1 (%2, %3) in %4").arg(p->partName().toQString(), p->instrumentId().toQString(),
                                                                         mainName, s.templateKey));
                QStringList have;   // (as offerLowAlternates: in a 7-Horn section, only parts from the main one on)
                const int from = s.templateKey == "7-horn" ? int(s.partIds.indexOf(pid)) : 0;
                for (int i = std::max(0, from); i < s.partIds.size(); ++i) {
                    if (const engraving::Part* a = ms->partById(ID(s.partIds.at(i)))) {
                        if (s.partIds.at(i) != pid) {
                            have << starscore::bandHornName(a->instrumentId().toQString());
                        }
                    }
                }
                QStringList missing;
                for (const StarScoreHornChoice& c : versionsFor(p->instrumentId().toQString(), s.templateKey)) {
                    if (!have.contains(c.bandName)) {
                        missing << c.instrumentId;
                    }
                }
                autotestLog("  missing: " + missing.join(", "));
                if (!missing.isEmpty()) {
                    const RetVal<QStringList> made = createLowAlternates(s.id, pid, missing);
                    autotestLog(QString("  made: %1 (%2)").arg(made.ret ? made.val.join(", ") : QString("failed"),
                                                               QString::fromStdString(made.ret.toString())));
                }
            }
        }
    } else if (step.startsWith("notes:")) {
        // notes:<part name> logs the part's first notes: sounding pitch, written pitch and spelling (tpc)
        const QString name = step.mid(QString("notes:").size());
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() != name || p->staves().empty()) {
                continue;
            }
            QStringList out;
            const engraving::staff_idx_t st = p->staves().front()->idx();
            for (engraving::Segment* seg = ms->firstMeasure()->first(engraving::SegmentType::ChordRest);
                 seg && out.size() < 16; seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::track_idx_t t = st * engraving::VOICES; t < (st + 1) * engraving::VOICES; ++t) {
                    engraving::EngravingItem* e = seg->element(t);
                    if (e && e->isChord()) {
                        for (const engraving::Note* n : engraving::toChord(e)->notes()) {
                            out << QString("%1/%2/%3").arg(n->pitch()).arg(n->pitch() - n->transposition()).arg(n->tpc());
                        }
                    }
                }
            }
            autotestLog(QString("  notes of %1 (%2): %3").arg(name, p->instrumentId().toQString(), out.join(' ')));
        }
    } else if (step.startsWith("section:")) {
        // section:<template key>[:<doubler id>[:<low horn id>]], e.g. section:2-horn-any (a 2-Horn Flexible section) or
        // section:7-horn:bb-clarinet:tuba, as from the Add section menu / the New StarScore choices
        const QStringList a = step.mid(QString("section:").size()).split(':');
        const QString key = a.value(0);
        const RetVal<QString> made = createSectionFromTemplate(key, a.value(1), a.value(2));
        autotestLog(QString("  %1: %2").arg(key, made.ret ? made.val : QString::fromStdString(made.ret.toString())));
    } else if (step.startsWith("flexsheet:")) {
        // flexsheet:<section template key>|<sheet name>, e.g. flexsheet:3-horn-any|Horn 1 - Alto Sax: that sheet made
        // into a part of its own, as the section menu's "Edit one sheet by hand" does
        const QString arg = step.mid(QString("flexsheet:").size());
        const QString key = arg.section('|', 0, 0), sheet = arg.section('|', 1);
        for (const StarScoreSection& s : load().sections) {
            if (s.templateKey == key) {
                const RetVal<QString> made = makeFlexibleSheetPart(s.id, sheet);
                autotestLog(QString("  %1: %2").arg(sheet, made.ret ? made.val : QString::fromStdString(made.ret.toString())));
            }
        }
    } else if (step.startsWith("home")) {
        // home or home:<section>: the Home page (for a screenshot)
        const QString section = step.section(':', 1);
        interactive()->open(UriQuery(section.isEmpty() ? std::string("musescore://home")
                                     : ("musescore://home?section=" + section.toStdString())));
        autotestLog("  home " + section);
    } else if (step.startsWith("uri:")) {
        // uri:<uri>: opens it (uri:muse://preferences); not waited for
        interactive()->open(UriQuery(step.mid(4).toStdString()));
        autotestLog("  opened " + step.mid(4));
    } else if (step.startsWith("finish:")) {
        // finish:<part name>: that part marked Finished, as the status menu does (1-Horn Trumpet: makes the others)
        const QString name = step.mid(7);
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == name) {
                setPartStatus(p->id().toQString(), int(StarScoreStatus::Finished));
                autotestLog("  finished " + name);
            }
        }
    } else if (step.startsWith("copypart:")) {
        // copypart:<from part>|<to part>: the music of one part pasted into another (to test with real music)
        const QString from = step.mid(9).section('|', 0, 0), to = step.mid(9).section('|', 1);
        engraving::Part* a = nullptr;
        engraving::Part* b = nullptr;
        for (engraving::Part* p : ms->parts()) {
            a = p->partName().toQString() == from ? p : a;
            b = p->partName().toQString() == to ? p : b;
        }
        if (a && b) {
            engraving::Segment* start = ms->firstMeasure()->first(engraving::SegmentType::ChordRest);
            engraving::Selection sel(ms);
            sel.setRange(start, nullptr, a->staves().front()->idx(), a->staves().front()->idx() + 1);
            const ByteArray mime = sel.mimeData();
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy part"));
            engraving::XmlReader reader(mime);
            ms->pasteStaff(reader, start, b->staves().front()->idx());
            master->notation()->undoStack()->commitChanges();
            autotestLog("  copied " + from + " to " + to);
        }
    } else if (step == "fillnotes") {
        // Test content (not music): quarter and eighth notes in each instrument's range on every staff, drum notes on
        // drum staves, a dynamic every 4 bars on every staff and a rehearsal mark every 8 bars
        unsigned seed = 7;
        auto rnd = [&seed](int n) {
            seed = seed * 1103515245u + 12345u;
            return int((seed >> 16) % unsigned(std::max(1, n)));
        };
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Test notes"));
        int bar = 0;
        int notes = 0;
        for (engraving::Measure* m = ms->firstMeasure(); m; m = m->nextMeasure(), ++bar) {
            for (engraving::Staff* st : ms->staves()) {
                if (st->isTabStaff(m->tick())) {
                    continue;
                }
                const engraving::Part* p = st->part();
                const engraving::Instrument* in = p->instrument(m->tick());
                const engraving::track_idx_t track = st->idx() * engraving::VOICES;
                int lo = in->minPitchA();
                int hi = in->maxPitchA();
                if (hi <= lo || hi - lo > 40) {
                    lo = 55;
                    hi = 79;
                }
                if (p->nstaves() > 1) {   // a grand staff: right hand high, left hand low
                    lo = st->rstaff() == 0 ? 60 : 36;
                    hi = st->rstaff() == 0 ? 84 : 59;
                }
                const engraving::Drumset* ds = in->drumset();
                engraving::Fraction t = m->tick();
                const engraving::Fraction end = m->endTick();
                while (t < end) {
                    engraving::Fraction d = rnd(3) == 0 ? engraving::Fraction(1, 8) : engraving::Fraction(1, 4);
                    if (t + d > end) {
                        d = end - t;
                    }
                    engraving::Segment* seg = ms->tick2segment(t, true, engraving::SegmentType::ChordRest);
                    if (!seg) {
                        break;
                    }
                    int pitch = lo + rnd(hi - lo + 1);
                    if (ds) {
                        static const int KIT[] = { 36, 38, 42, 46, 49, 51, 60, 62 };
                        pitch = -1;
                        for (int k = 0; k < 16 && pitch < 0; ++k) {
                            const int c = KIT[rnd(8)];
                            pitch = ds->isValid(c) ? c : -1;
                        }
                        for (int c = 0; c < 128 && pitch < 0; ++c) {
                            pitch = ds->isValid(c) ? c : -1;
                        }
                    }
                    if (pitch >= 0) {
                        ms->setNoteRest(seg, track, engraving::NoteVal(pitch), d);
                        ++notes;
                    }
                    t += d;
                }
                if (bar % 4 == 0) {
                    if (engraving::Segment* s0 = m->first(engraving::SegmentType::ChordRest)) {
                        engraving::Dynamic* dyn = engraving::Factory::createDynamic(s0);
                        dyn->setDynamicType(bar % 8 == 0 ? engraving::DynamicType::MF : engraving::DynamicType::F);
                        dyn->setTrack(track);
                        dyn->setParent(s0);
                        ms->undoAddElement(dyn);
                    }
                }
            }
            if (bar % 8 == 0) {
                if (engraving::Segment* s0 = m->first(engraving::SegmentType::ChordRest)) {
                    engraving::RehearsalMark* rm = engraving::Factory::createRehearsalMark(s0);
                    rm->setTrack(0);
                    rm->setXmlText(String(QString(QChar('A' + bar / 8))));
                    rm->setParent(s0);
                    ms->undoAddElement(rm);
                }
            }
        }
        master->notation()->undoStack()->commitChanges();
        master->notation()->notationChanged().notify();
        autotestLog(QString("  %1 notes in %2 bars").arg(notes).arg(bar));
    } else if (step.startsWith("addtext:")) {
        // a staff text on a part's first bar: addtext:<part name>|<text>
        const QString partName = step.mid(8).section('|', 0, 0);
        const QString text = step.section('|', 1);
        for (engraving::Part* p : ms->parts()) {
            engraving::Segment* s0 = ms->firstMeasure() ? ms->firstMeasure()->first(engraving::SegmentType::ChordRest) : nullptr;
            if (p->partName().toQString() != partName || !s0) {
                continue;
            }
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Autotest text"));
            engraving::StaffText* st = engraving::Factory::createStaffText(s0);
            st->setTrack(p->startTrack());
            st->setXmlText(String(text));
            st->setParent(s0);
            ms->undoAddElement(st);
            master->notation()->undoStack()->commitChanges();
            autotestLog(QString("  added '%1' to %2").arg(text, partName));
        }
    } else if (step == "parts") {
        for (const engraving::Part* p : ms->parts()) {
            autotestLog(QString("  %1 (%2)").arg(p->partName().toQString(), p->instrumentId().toQString()));
        }
    } else if (step == "status") {
        // each arrangement's status (0 Empty … 4 Finished) and each section's
        const Data d = load();
        for (const StarScoreSection& s : d.sections) {
            autotestLog(QString("  section %1: %2 (skips %3)").arg(s.name).arg(int(s.status)).arg(s.autoSkipSheets.join(",")));
        }
        for (const StarScoreArrangement& a : d.arrangements) {
            autotestLog(QString("  arrangement %1: %2").arg(a.name).arg(int(arrangementStatus(a.id))));
        }
    } else if (step.startsWith("sectionon:") || step.startsWith("sectionoff:")) {
        // the View panel's switch for one section, then which instruments show
        const QString id = step.section(':', 1);
        setSectionOn(id, step.startsWith("sectionon:"));
        QStringList showing;
        for (const engraving::Part* p : ms->parts()) {
            if (p->show()) {
                showing << p->partName().toQString();
            }
        }
        autotestLog(QString("  showing: %1").arg(showing.join(", ")));
        QStringList tabs;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->notation() && e->notation()->isOpen()) {
                tabs << e->name();
            }
        }
        autotestLog(QString("  open tabs: %1").arg(tabs.join(", ")));
    } else if (step == "dashboard") {
        // what Home › Dashboard reads for this file: each arrangement's column, status, audited
        const QString path = globalContext()->currentProject() ? globalContext()->currentProject()->path().toQString() : QString();
        const StarScoreAuditFileSummary s = auditFile(path, true);
        autotestLog(QString("  %1 error '%2'").arg(path, s.error));
        for (const StarScoreFileArrangement& a : s.arrangementList) {
            autotestLog(QString("  arr %1 [%2]: status %3 audited %4 changed %5 issues %6 unfinished %7")
                        .arg(a.name, a.column).arg(a.status).arg(a.audited).arg(a.changedSinceAudit).arg(a.openIssues)
                        .arg(a.unfinished.join("; ")));
        }
    } else if (step == "demos") {
        // Export Audio Demos, as the export window's checkbox does
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty() && QDir(band).exists()) {
            setBandFolder(band);
        }
        const RetVal<QString> r = exportAudioDemos();
        autotestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.toString())));
    } else if (step.startsWith("songbooks:")) {
        // songbooks:1 / songbooks:0: Preferences › General › "Show the Songbooks page on Home"
        settings()->setSharedValue(Settings::Key("project", "starscore/showSongbooks"), Val(step.endsWith("1")));
        autotestLog("  songbooks shown: " + step.section(':', 1));
    } else if (step.startsWith("open:")) {
        // open:<part score name>: that part score shown (the first whose name contains the text)
        const QString name = step.mid(5);
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (INotationPtr n = e->notation(); n && e->name().contains(name)) {
                master->setExcerptIsOpen(n, true);
                globalContext()->setCurrentNotation(n);
                QCoreApplication::processEvents();
                autotestLog("  opened " + e->name());
                break;
            }
        }
    } else if (step.startsWith("flexview:")) {
        // flexview:<mode>: every Flexible chair shown in that view (StarScoreFlexibleView), then each chair's clef and
        // transposition
        setFlexibleViewMode(step.section(':', 1).toInt());
        for (const StarScoreSection& s : load().sections) {
            if (!s.templateKey.endsWith("-horn-any")) {
                continue;
            }
            for (const QString& pid : s.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (p && !p->staves().empty()) {
                    const engraving::ClefTypeList c = p->staves().front()->defaultClefType();
                    autotestLog(QString("  %1: clef %2/%3 transpose %4")
                                .arg(p->partName().toQString()).arg(int(c.concertClef)).arg(int(c.transposingClef))
                                .arg(p->instrument()->transpose().chromatic));
                }
            }
        }
    } else if (step.startsWith("clefs:")) {
        // clefs:<section template key>: each part's clef (concert/transposing), transposition and first written note
        const QString key = step.section(':', 1, 1);
        engraving::MasterScore* ms = masterScore();
        for (const StarScoreSection& s : load().sections) {
            if (s.templateKey != key || !ms) {
                continue;
            }
            for (const QString& pid : s.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (!p || p->staves().empty()) {
                    continue;
                }
                const engraving::Staff* st = p->staves().front();
                QStringList linked;
                for (const engraving::Staff* ls : st->staffList()) {
                    linked << QString("%1/%2").arg(int(ls->defaultClefType().concertClef)).arg(int(ls->defaultClefType().transposingClef));
                }
                const engraving::Interval v = p->instrument()->transpose();
                autotestLog(QString("  %1: transpose %2/%3, clefs (concert/written, per linked staff) %4")
                            .arg(p->partName().toQString()).arg(v.diatonic).arg(v.chromatic).arg(linked.join(" ")));
            }
        }
    } else if (step == "suggest") {
        // the version number the export dialog would tick, and why
        const RetVal<StarScoreBandExportPlan> plan = planBandExport();
        if (plan.ret) {
            const StarScoreVersionSuggestion sg = suggestVersionBump(plan.val);
            autotestLog(QString("  suggested bump %1: %2").arg(sg.bump).arg(sg.reasons.join(" | ")));
        } else {
            autotestLog("  no plan: " + QString::fromStdString(plan.ret.toString()));
        }
    } else if (step.startsWith("arrangement:")) {
        // arrangement:<arrangement key>[:<doubler id>[:<low horn id>]], as the StarScore bar's Add arrangement menu makes
        // it (with the Add arrangement dialog's choices)
        const QStringList a = step.mid(QString("arrangement:").size()).split(':');
        const RetVal<QString> made = createArrangementFromTemplate(a.value(0), a.value(1), a.value(2));
        autotestLog(QString("  %1: %2").arg(a.value(0), made.ret ? made.val : QString::fromStdString(made.ret.toString())));
    } else if (step.startsWith("new:")) {
        // new:<arrangement key>[:<doubler id>[:<low horn id>]]: a new StarScore as File › New makes it (title "Autotest"),
        // which replaces the open song; its parts and sections are logged
        const QStringList a = step.mid(QString("new:").size()).split(':');
        StarScoreNewOptions o;
        o.title = "Autotest";
        o.arrangementTemplateKey = a.value(0);
        o.doublerInstrumentId = a.value(1);
        o.lowHornInstrumentId = a.value(2);
        // The app makes a new score in a window with no project open. Here the open song is let go first, but kept
        // alive until the new one is current: the status bar still points at its notation and disconnects from it
        // when the project changes, which crashed when the song had already been destroyed.
        const INotationProjectPtr old = globalContext()->currentProject();
        globalContext()->setCurrentProject(nullptr);
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
        }
        const Ret r = newStarScore(o);
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
        }
        (void)old;
        autotestLog(QString("  new %1: %2").arg(a.value(0), r ? "ok" : QString::fromStdString(r.toString())));
        // the dialog's doubler label takes the player's name from the band folder's roster (logged as found or not,
        // never the name)
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty() && QDir(band).exists()) {
            setBandFolder(band);
        }
        autotestLog(QString("  roster doubler: %1").arg(rosterDoublerName().isEmpty() ? "(none)" : "(found)"));
        if (engraving::MasterScore* nms = masterScore()) {
            for (const engraving::Part* p : nms->parts()) {
                autotestLog(QString("  part %1: %2 (%3)").arg(idText(p), p->partName().toQString(), p->instrumentId().toQString()));
            }
            for (const StarScoreSection& s : load().sections) {
                autotestLog(QString("  section %1 (%2): %3").arg(s.id, s.templateKey, s.partIds.join(", ")));
            }
        }
    } else if (step == "styles") {
        autotestLog(QString("  restyled %1").arg(applyStyles()));
    } else if (step.startsWith("version:")) {
        // version:1.2.3: the version number set in the song and every part score's footer (as an export does first)
        setScoreVersion(step.mid(8));
        autotestLog(QString("  version %1").arg(scoreVersion()));
    } else if (step == "export") {
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (band.isEmpty() || !QDir(band).exists()) {
            autotestLog("  STARSCORE_AUTOTEST_BAND missing");
        } else {
            setBandFolder(band);
            const RetVal<QString> r = exportToBandFolder({});
            QString text = r.ret ? r.val : QString::fromStdString(r.ret.toString());
            text.replace("\n", "\n  ");
            autotestLog("  " + text);
        }
    } else if (step.startsWith("updateall:")) {
        // updateall:<Projects and Sheets folder>: "Update all sheets" on every song it offers (then waitupdate)
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty()) {
            setBandFolder(band);
        }
        setAuditLibraryFolder(step.mid(10));
        QStringList paths;
        for (const StarScoreOutdatedSong& o : outdatedSongs()) {
            paths << o.path;
        }
        autotestLog(QString("  %1 song(s): %2").arg(paths.size()).arg(paths.join(", ")));
        startUpdateAllSheets(paths);
    } else if (step == "personnames") {
        // personnames: part scores named after players renamed (also done when a song opens); then lists them all
        autotestLog(QString("  %1 renamed").arg(renamePersonNamedPartScores()));
        if (IMasterNotationPtr master = globalContext()->currentMasterNotation()) {
            QStringList names;
            for (const IExcerptNotationPtr& e : master->excerpts()) {
                names << e->name();
            }
            autotestLog("  part scores: " + names.join(" | "));
        }
    } else if (step == "standardize" || step == "standardize:plan") {
        // standardize: the song made standard (standardize:plan only lists what that would change)
        const QStringList lines = standardizeSong(step == "standardize");
        autotestLog(QString("  %1 change(s)").arg(lines.size()));
        for (const QString& l : lines) {
            autotestLog("  - " + l);
        }
        if (step == "standardize") {
            if (engraving::MasterScore* nms = masterScore()) {
                for (const StarScoreSection& s : load().sections) {
                    QStringList names;
                    for (const QString& pid : s.partIds) {
                        const engraving::Part* p = nms->partById(ID(pid));
                        names << (p ? p->partName().toQString() : pid);
                    }
                    autotestLog(QString("  section %1 \"%2\" (%3): %4").arg(s.id, s.name, s.templateKey, names.join(", ")));
                }
                for (const StarScoreArrangement& a : load().arrangements) {
                    autotestLog(QString("  arrangement \"%1\" (%2) score \"%3\": %4").arg(a.name, a.templateKey, a.scoreName, a.sectionIds.join(", ")));
                }
            }
            if (IMasterNotationPtr master = globalContext()->currentMasterNotation()) {
                QStringList names;
                for (const IExcerptNotationPtr& e : master->excerpts()) {
                    names << e->name();
                }
                autotestLog("  part scores: " + names.join(" | "));
            }
        }
    } else if (step.startsWith("timevariants:")) {
        // timevariants:half|double|both: the Half-Time / Double-Time sheets (every sheet) into STARSCORE_AUTOTEST_BAND
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty()) {
            setBandFolder(band);
        }
        const QString which = step.mid(13);
        const RetVal<QString> r = exportTimeVariants({}, which != "double", which != "half");
        QString text = r.ret ? r.val : QString::fromStdString(r.ret.toString());
        text.replace("\n", "\n  ");
        autotestLog("  " + text);
    } else if (step == "todouble") {
        // todouble: the open song rewritten at twice its note lengths (what the Double-Time sheets are made from)
        const RetVal<QString> r = convertTimeOf(globalContext()->currentProject(), true, true);
        autotestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.toString())));
    } else if (step == "doubletime") {
        // doubletime: "Convert from double time" on the open song
        const RetVal<QString> r = convertFromDoubleTime();
        autotestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.toString())));
    } else if (step.startsWith("renamelib:")) {
        // renamelib:<Projects and Sheets folder>: what StarScore does when it starts (Title.starscore -> CODE - Title)
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty()) {
            setBandFolder(band);
        }
        setAuditLibraryFolder(step.mid(10));
        autotestLog(QString("  %1 renamed").arg(renameLibraryFilesToCodes()));
    } else if (step.startsWith("stylesall:")) {
        // stylesall:<Projects and Sheets folder>: "Apply part styles to every song" (then waitupdate)
        setAuditLibraryFolder(step.mid(10));
        const QStringList paths = auditLibraryFiles(step.mid(10));
        autotestLog(QString("  %1 song(s)").arg(paths.size()));
        startApplyStylesToAll(paths);
    } else if (step.startsWith("exportonly:") || step == "updatesong" || step.startsWith("outdated:")) {
        // exportonly:<path>+<path>: only those sheets; updatesong: "Update all sheets" for the open song;
        // outdated:<Projects and Sheets folder>: the songs Update all sheets would offer
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty()) {
            setBandFolder(band);
        }
        RetVal<QString> r;
        if (step.startsWith("exportonly:")) {
            r = exportToBandFolder(step.mid(11).split('+'));
        } else if (step == "updatesong") {
            r = updateCurrentSongSheets();
        } else {
            setAuditLibraryFolder(step.mid(9));
            QStringList lines;
            for (const StarScoreOutdatedSong& o : outdatedSongs()) {
                lines << QString("%1 %2 format %3 with %4: %5 finished").arg(o.code, o.title).arg(o.sheetFormat)
                         .arg(o.exportedWith).arg(o.finishedSheets);
            }
            r = RetVal<QString>::make_ok(QString("%1 outdated\n").arg(lines.size()) + lines.join("\n"));
        }
        QString text = r.ret ? r.val : QString::fromStdString(r.ret.toString());
        text.replace("\n", "\n  ");
        autotestLog("  " + text);
    } else if (step == "chords") {
        INotationProjectPtr project = globalContext()->currentProject();
        const QString path = project ? project->path().toQString() : QString();
        const starscore::ChordChartData data = starscore::extractChordChart(ms);
        QFile json(autotestDir() + "/chords.json");
        if (json.open(QIODevice::WriteOnly)) {
            json.write(QJsonDocument(data.toJson()).toJson(QJsonDocument::Indented));
        }
        QString why;
        if (!starscore::chordChartPossible(data, &why)) {
            autotestLog("  no chart: " + why);
        } else {
            // the title and code the prototype's render.py took from the file name
            const QString title = starscore::chordChartTitleFromFileName(path);
            const QRegularExpressionMatch codeMatch = QRegularExpression("^([A-Z]{4}) - ").match(QFileInfo(path).fileName());
            QString code = codeMatch.hasMatch() ? codeMatch.captured(1) : QString(title).remove(QRegularExpression("\\W")).left(4).toUpper();
            starscore::ChordChartFonts fonts = starscore::chordChartEmbeddedFonts();
            const QStringList urls = qEnvironmentVariable("STARSCORE_AUTOTEST_FONTS").split(';');
            if (urls.size() == 3) {
                fonts.modernoir = urls[0];
                fonts.jost = urls[1];
                fonts.bravura = urls[2];
            }
            auto write = [&](const QString& name, const QString& text) {
                QFile f(autotestDir() + "/" + name);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(text.toUtf8());
                }
            };
            write("chart.html", starscore::chordChartHtml(data, title, fonts, QString()));
            write("ireal.html", starscore::chordChartIRealHtml(data, title, QString()));
            write(code + " - Chord Chart.html", starscore::chordChartHtml(data, title, fonts, scoreVersion()));
            autotestLog(QString("  wrote chords.json, chart.html, ireal.html (%1 bars, lead %2, status %3)")
                        .arg(data.measures.size()).arg(data.lead, data.leadStatus.value_or("none")));
        }
    } else if (step.startsWith("rechord:")) {
        // rechord:<marks>|<from>|<to>: in the bars from each rehearsal mark (e.g. "G+J") to the next one, in the main
        // score and every part score, each chord symbol whose extension is exactly <from> (e.g. "^9") gets <to>
        // ("^7"); the root and bass stay (so a transposed part keeps its own). <to> empty: only lists the chords.
        const QString arg = step.mid(8);
        const QStringList marks = arg.section('|', 0, 0).split('+');
        const QString from = arg.section('|', 1, 1), to = arg.section('|', 2, 2);
        // the tick ranges, from the main score's rehearsal marks
        std::vector<std::pair<int, int> > ranges;
        std::vector<std::pair<int, QString> > all;
        for (const engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
             seg = seg->next1(engraving::SegmentType::ChordRest)) {
            for (const engraving::EngravingItem* a : seg->annotations()) {
                if (a->isRehearsalMark() && (all.empty() || all.back().first != seg->tick().ticks())) {
                    all.push_back({ seg->tick().ticks(), engraving::toRehearsalMark(a)->plainText().toQString().trimmed() });
                }
            }
        }
        for (size_t i = 0; i < all.size(); ++i) {
            if (marks.contains(all[i].second)) {
                const int end = i + 1 < all.size() ? all[i + 1].first : ms->endTick().ticks();
                ranges.push_back({ all[i].first, end });
                autotestLog(QString("  %1: ticks %2-%3").arg(all[i].second).arg(all[i].first).arg(end));
            }
        }
        std::vector<engraving::Score*> scores { ms };
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->notation()) {
                scores.push_back(e->notation()->elements()->msScore());
            }
        }
        static const QRegularExpression name("^([A-Ga-g](?:b|#)?)(.*?)(/[A-Ga-g](?:b|#)?)?$");
        int changed = 0;
        for (engraving::Score* sc : scores) {
            QStringList seen;
            for (engraving::Segment* seg = sc->firstSegment(engraving::SegmentType::ChordRest); seg;
                 seg = seg->next1(engraving::SegmentType::ChordRest)) {
                const int t = seg->tick().ticks();
                if (std::none_of(ranges.begin(), ranges.end(), [t](const auto& r) { return t >= r.first && t < r.second; })) {
                    continue;
                }
                for (engraving::EngravingItem* a : seg->annotations()) {
                    if (!a->isHarmony()) {
                        continue;
                    }
                    engraving::Harmony* h = engraving::toHarmony(a);
                    const QString old = h->harmonyName().toQString();
                    const QRegularExpressionMatch m = name.match(old);
                    QString now = old;
                    if (!to.isEmpty() && m.hasMatch() && m.captured(2) == from) {
                        // (a root kept in lower case is written in capitals: these are major chords)
                        QString root = m.captured(1);
                        root[0] = root[0].toUpper();
                        now = root + to + m.captured(3);
                        h->setPlainText(String::fromQString(now));
                        h->setHarmony(String::fromQString(now));
                        h->triggerLayout();
                        ++changed;
                    }
                    seen << QString("%1:%2%3").arg(t).arg(old).arg(now != old ? "->" + now : QString());
                }
            }
            autotestLog(QString("  [%1] %2").arg(sc->isMaster() ? QString("main score") : sc->excerpt()->name().toQString(),
                                                 seen.join("  ")));
        }
        if (changed) {
            ms->setLayoutAll();
            ms->doLayout();
            for (engraving::Score* sc : scores) {
                sc->setLayoutAll();
                sc->doLayout();
            }
            if (INotationProjectPtr project = globalContext()->currentProject()) {
                project->markAsUnsaved();
            }
        }
        autotestLog(QString("  changed %1 chord symbols").arg(changed));
    } else if (step == "save") {
        INotationProjectPtr project = globalContext()->currentProject();
        const Ret r = project ? project->save(io::path_t(autotestDir() + "/saved.starscore"), SaveMode::SaveCopy, false)
                      : make_ret(Ret::Code::InternalError);
        autotestLog(QString("  saved: %1").arg(r ? "ok" : QString::fromStdString(r.toString())));
    } else {
        autotestLog("  unknown step");
    }
    autotestLog(QString("  took %1 ms").arg(stepClock.elapsed()));

    QTimer::singleShot(500, &m_timerGuard, [this, steps, reportNumber]() { runAutotestSteps(steps, reportNumber); });
}

void StarScoreService::autotestReport(const QString& base)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    const Data data = load();
    QJsonObject out;

    QJsonArray parts;
    for (const engraving::Part* p : ms->parts()) {
        QJsonArray staves;
        for (const engraving::Staff* st : p->staves()) {
            staves.append(st->visible());
        }
        parts.append(QJsonObject { { "id", idText(p) }, { "name", p->partName().toQString() },
                                   { "instrument", p->instrumentId().toQString() }, { "show", p->show() },
                                   { "staffVisible", staves } });
    }
    out["parts"] = parts;

    QJsonArray sections;
    for (const StarScoreSection& s : data.sections) {
        QJsonObject alts;
        for (const auto& [a, m] : s.alternates) {
            alts[a] = m;
        }
        sections.append(QJsonObject { { "id", s.id }, { "name", s.name }, { "key", s.templateKey },
                                      { "parts", QJsonArray::fromStringList(s.partIds) },
                                      { "shown", QJsonArray::fromStringList(s.shownPartIds) }, { "alternates", alts } });
    }
    out["sections"] = sections;
    out["barlinesDiffering"] = starscore::syncEndBarlines(ms, false);

    const QString pdfDir = base + "-parts";
    QDir().mkpath(pdfDir);
    QJsonArray books;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        INotationPtr n = e->notation();
        engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (!es) {
            continue;
        }
        es->doLayout();
        QJsonObject b { { "name", e->name() }, { "parts", int(es->parts().size()) } };
        QJsonArray texts;
        const engraving::Text* comp = nullptr;
        const engraving::Text* label = nullptr;
        for (engraving::MeasureBase* mb = es->first(); mb && !mb->isMeasure(); mb = mb->next()) {
            if (!mb->isVBox()) {
                continue;
            }
            b["frameHeight"] = mb->getProperty(engraving::Pid::BOX_HEIGHT).value<engraving::Spatium>().val();
            b["frameAutosize"] = mb->getProperty(engraving::Pid::BOX_AUTOSIZE).toBool();
            b["frameRect"] = rectJson(mb->pageBoundingRect());
            for (engraving::EngravingItem* el : mb->el()) {
                if (!el || !el->isText()) {
                    continue;
                }
                const engraving::Text* t = engraving::toText(el);
                const bool right = t->textStyleType() == engraving::TextStyleType::INSTRUMENT_EXCERPT
                                   && t->position() == engraving::AlignH::RIGHT;
                if (t->textStyleType() == engraving::TextStyleType::COMPOSER) {
                    comp = t;
                }
                if (right) {
                    label = t;
                }
                texts.append(QJsonObject {
                    { "style", int(t->textStyleType()) },
                    { "text", t->xmlText().toQString() }, { "rightLabel", right },
                    { "offsetX", t->offset().x() }, { "offsetY", t->offset().y() },
                    { "ownOffset", t->propertyFlags(engraving::Pid::OFFSET) == engraving::PropertyFlags::UNSTYLED },
                    { "rect", rectJson(t->pageBoundingRect()) } });
            }
            break;
        }
        b["texts"] = texts;
        const engraving::PointF co = es->style().styleV(engraving::Sid::composerOffset).value<engraving::PointF>();
        b["composerOffsetStyle"] = QJsonObject { { "x", co.x() }, { "y", co.y() } };
        if (comp && label) {
            const engraving::RectF c = comp->pageBoundingRect(), l = label->pageBoundingRect();
            b["composerMeetsLabel"] = c.top() < l.bottom() && c.bottom() > l.top();
            b["gapBelowLabel"] = c.top() - l.bottom();
        }
        books.append(b);
        if (es->parts().size() == 1) {
            writePdf(n, pdfDir + "/" + safeName(e->name()) + ".pdf");
        }
    }
    out["partScores"] = books;

    QFile f(base + ".json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(out).toJson(QJsonDocument::Indented));
    }
    autotestLog("  wrote " + base + ".json");
}
