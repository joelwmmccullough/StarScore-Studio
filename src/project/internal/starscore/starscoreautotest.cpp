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
 *   tabs          lists the part scores open as tabs, and the closed ones
 *   chordsymbols:<staff>:<chord>|<chord>   types chord symbols on bars 1, 2, … of a staff; logs what each score shows
 *   sectionbreaks the bars with a section break, score by score; addsectionbreak:<part score>:<bar> and
 *                 removesectionbreak:<part score>:<bar> add or take away one first
 *   version:X     sets the version number (version:1.2.3) in the song and every part score's footer
 *   export        Export to Sheets and Demos into STARSCORE_AUTOTEST_BAND (a copy, never the real folder);
 *                 export:<text>[|<text>…] only the sheets whose path holds one of the texts
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
#include "starscorelayoutcheck.h"
#include "starscorehouse.h"
#include "engraving/editing/editproperty.h"
#include "starscorehornkeys.h"
#include "starscoreautoarchive.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/system.h"
#include "engraving/dom/textbase.h"
#include "engraving/dom/page.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/mmrest.h"
#include "global/serialization/zipreader.h"
#include "global/serialization/zipwriter.h"
#include <QElapsedTimer>
#include <algorithm>
#include "engraving/dom/starscoreprogress.h"
#include "engraving/dom/starscoreannotations.h"
#include "starscoreannotations.h"
#include "engraving/dom/breath.h"
#include "engraving/dom/fermata.h"
#include "engraving/dom/fingering.h"
#include "engraving/dom/hairpin.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/beam.h"
#include "engraving/dom/rest.h"
#include "starscoreengraving.h"
#include "starscorechordchart.h"
#include "organizer/organizer.h"
#include "organizer/orghtml.h"
#include "organizer/orglibrary.h"
#include "starscorepractice.h"
#include "starscorepracticebook.h"
#include <QEventLoop>

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
#include "io/buffer.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <cstdio>
#include <set>
#include <cstdlib>

#include "settings.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/linkedobjects.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/dom/select.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/stringdata.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/text.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/box.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/style/style.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationsolomutestate.h"
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
    } else if (step.startsWith("current:")) {
        // current:<part score name>: that part score open and showing (as clicking its tab)
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == step.mid(8) && e->notation()) {
                master->setExcerptIsOpen(e->notation(), true);
                globalContext()->setCurrentNotation(e->notation());
                autotestLog("  showing " + e->name());
            }
        }
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
    } else if (step.startsWith("segx:")) {
        // segx:<part score>|<bar>: that bar's segments in the part score (laid out): type, tick, x, width, stretch,
        // and the chords/rests in each (voice, duration, visible)
        const QString book = step.mid(5).section('|', 0, 0);
        const int bar = step.section('|', 1).toInt();
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() != book) {
                continue;
            }
            engraving::Score* es = e->notation()->elements()->msScore();
            es->doLayout();
            engraving::Measure* m = es->crMeasure(bar - 1);
            if (!m) {
                continue;
            }
            autotestLog(QString("  bar %1: x %2 width %3 stretch %4").arg(bar).arg(m->x()).arg(m->width()).arg(m->userStretch()));
            for (engraving::Segment* sg = m->first(); sg; sg = sg->next()) {
                QStringList what;
                for (engraving::track_idx_t t = 0; t < es->ntracks(); ++t) {
                    engraving::EngravingItem* el = sg->element(t);
                    if (el && el->isChordRest()) {
                        const engraving::ChordRest* cr = engraving::toChordRest(el);
                        what << QString("v%1 %2%3 x%4 w%5").arg(t % 4 + 1).arg(cr->durationTypeTicks().toString())
                            .arg(cr->visible() ? "" : " (hidden)").arg(cr->x(), 0, 'f', 2).arg(cr->shape().right(), 0, 'f', 2);
                        if (cr->isChord()) {
                            for (const engraving::Note* n : engraving::toChord(cr)->notes()) {
                                what << QString("note vis%1").arg(n->visible());
                            }
                        }
                    }
                }
                autotestLog(QString("    %1 tick %2 x %3 w %4 %5").arg(int(sg->segmentType()), 0, 16)
                            .arg(sg->rtick().toString()).arg(sg->x(), 0, 'f', 2).arg(sg->width(), 0, 'f', 2).arg(what.join("; ")));
            }
        }
    } else if (step.startsWith("linksync:")) {
        // linksync:<main|part score>|<part name>|<bar>|<note|acc|harmony|dynamic|rest|artic>|<visible|paren>:
        // in that score, the first such item of the part's bar selected and Toggle visible / Toggle parentheses done
        // on it as the menu does; then logs the item and its copies in every score: visible, parentheses
        const QStringList a = step.mid(9).split('|');
        engraving::Score* sc = nullptr;
        INotationPtr nt;
        if (a.value(0) == "main") {
            sc = ms;
            nt = master->notation();
        } else {
            for (const IExcerptNotationPtr& e : master->excerpts()) {
                if (e->name() == a.value(0)) {
                    nt = e->notation();
                    sc = nt->elements()->msScore();
                }
            }
        }
        engraving::Part* part = nullptr;
        for (engraving::Part* p : sc ? sc->parts() : std::vector<engraving::Part*>()) {
            if (p->partName().toQString() == a.value(1)) {
                part = p;
            }
        }
        if (!part && sc && sc->parts().size() == 1) {
            part = sc->parts().front();   // (a part score's part keeps its instrument's name: "Trumpet in B♭")
        }
        engraving::Measure* m = sc ? sc->crMeasure(a.value(2).toInt() - 1) : nullptr;
        engraving::EngravingItem* item = nullptr;
        if (part && m) {
            const engraving::track_idx_t t0 = part->startTrack();
            const engraving::track_idx_t t1 = part->endTrack();
            const QString what = a.value(3);
            for (engraving::Segment* sg = m->first(); sg && !item; sg = sg->next()) {
                for (engraving::EngravingItem* an : sg->annotations()) {
                    if (an->track() >= t0 && an->track() < t1
                        && ((what == "harmony" && an->isHarmony()) || (what == "dynamic" && an->isDynamic()))) {
                        item = an;
                        break;
                    }
                }
                for (engraving::track_idx_t t = t0; t < t1 && !item; ++t) {
                    engraving::EngravingItem* el = sg->element(t);
                    if (!el) {
                        continue;
                    }
                    if (what == "rest" && el->isRest()) {
                        item = el;
                    } else if (el->isChord()) {
                        engraving::Chord* c = engraving::toChord(el);
                        if (what == "note") {
                            item = c->upNote();
                        } else if (what == "acc") {
                            for (engraving::Note* n : c->notes()) {
                                if (n->accidental()) {
                                    item = n->accidental();
                                }
                            }
                        } else if (what == "artic" && !c->articulations().empty()) {
                            item = c->articulations().front();
                        }
                    }
                }
            }
        }
        if (!item) {
            QStringList names;
            for (engraving::Part* p : sc ? sc->parts() : std::vector<engraving::Part*>()) {
                names << p->partName().toQString();
            }
            autotestLog(QString("  nothing found (score %1, part %2, bar %3; parts: %4)").arg(sc != nullptr).arg(part != nullptr)
                        .arg(m != nullptr).arg(names.join(", ")));
        } else {
            sc->select(item);
            nt->undoStack()->prepareChanges(TranslatableString::untranslatable("linksync"));
            if (a.value(4) == "visible") {
                sc->cmdToggleVisible();
            } else {
                sc->cmdToggleParentheses();
            }
            nt->undoStack()->commitChanges();
            nt->notationChanged().notify();
            auto describe = [&](engraving::EngravingObject* o) {
                engraving::EngravingItem* e = engraving::toEngravingItem(o);
                QString where = e->score()->isMaster() ? QString("main") : QString();
                for (const IExcerptNotationPtr& ex : master->excerpts()) {
                    if (ex->notation()->elements()->msScore() == e->score()) {
                        where = ex->name();
                    }
                }
                QString paren = QString::number(int(e->getProperty(engraving::Pid::HAS_PARENTHESES).value<engraving::ParenthesesMode>()));
                if (e->isAccidental()) {
                    paren = "bracket " + QString::number(engraving::toAccidental(e)->getProperty(engraving::Pid::ACCIDENTAL_BRACKET).toInt());
                }
                return QString("%1: visible %2, parentheses %3, paren items %4%5").arg(where).arg(e->visible()).arg(paren)
                       .arg((e->leftParen() ? 1 : 0) + (e->rightParen() ? 1 : 0))
                       .arg(e->isPropertyLinkedToMaster(engraving::Pid::VISIBLE) ? "" : " (visible unlinked)");
            };
            autotestLog(QString("  %1 in %2:").arg(item->typeName(), a.value(0)));
            if (item->isAccidental() && item->explicitParent() && item->explicitParent()->links()) {
                // (an accidental has no copies of its own: the note's copies' accidentals)
                for (engraving::EngravingObject* o : *item->explicitParent()->links()) {
                    if (engraving::Accidental* acc = engraving::toNote(o)->accidental()) {
                        autotestLog("    " + describe(acc));
                    }
                }
            } else if (item->links()) {
                for (engraving::EngravingObject* o : *item->links()) {
                    autotestLog("    " + describe(o));
                }
            } else {
                autotestLog("    " + describe(item) + " (no copies)");
            }
        }
    } else if (step.startsWith("barmmr:")) {
        // barmmr:<part score>|<bar>|<0/1>: that bar's "Break multimeasure rests" in the part score (only there);
        // barmmr:<part score>: lists the bars that break them
        const QStringList a = step.mid(7).split('|');
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() != a.value(0)) {
                continue;
            }
            engraving::Score* es = e->notation()->elements()->msScore();
            if (a.size() >= 3) {
                if (engraving::Measure* m = es->crMeasure(a.value(1).toInt() - 1)) {
                    e->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("barmmr"));
                    es->undo(new engraving::ChangeProperty(m, engraving::Pid::BREAK_MMR, a.value(2) == "1"));
                    e->notation()->undoStack()->commitChanges();
                }
            }
            QStringList bars;
            int i = 1;
            for (engraving::Measure* m = es->firstMeasure(); m; m = m->nextMeasure(), ++i) {
                if (m->breakMultiMeasureRest()) {
                    bars << QString::number(i);
                }
            }
            autotestLog(QString("  %1: breaks multimeasure rests at bars %2").arg(e->name(), bars.join(", ")));
        }
    } else if (step == "stylecheck") {
        // stylecheck: whether opening the song would offer the default style, and the settings that differ
        const Data d = load();
        autotestLog(QString("  default %1 (%2); song restyled with %3; differs in %4: %5").arg(loadStyleSettings().defaultStyle)
                    .arg(defaultStyleFingerprint()).arg(d.styleFingerprint.isEmpty() ? QString("(no record)") : d.styleFingerprint)
                    .arg(styleDifferences().size()).arg(styleDifferences().mid(0, 12).join(", ")));
    } else if (step.startsWith("copyrightyear:")) {
        // copyrightyear:<year>: as the "…" menu's Copyright year
        setCopyrightYear(step.mid(14).toInt());
        autotestLog(QString("  year %1; notice: %2").arg(copyrightYear()).arg(ms->metaTag(u"copyright").toQString()));
    } else if (step == "copyright") {
        // copyright: the copyright line the song's sheets get, and what it was made from
        if (const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND"); !band.isEmpty()) {
            setBandFolder(band);
        }
        syncCopyrightNow(false);
        autotestLog(QString("  title: %1 | composers: %2 | %3 | year %4").arg(starscore::songTitle(ms))
                    .arg(starscore::composerNames(ms).join(" + ")).arg(songIsCover() ? "cover" : "original")
                    .arg(load().copyrightYear));
        autotestLog("  notice: " + ms->metaTag(u"copyright").toQString());
    } else if (step.startsWith("mimedump:")) {
        // mimedump:<part name>|B1-B2: the clipboard text of those bars of the part's first staff, to mime-<n>.xml
        const QString name = step.mid(9).section('|', 0, 0);
        const QString bars = step.section('|', 1);
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() != name) {
                continue;
            }
            engraving::Measure* m1 = ms->crMeasure(bars.section('-', 0, 0).toInt() - 1);
            engraving::Measure* m2 = ms->crMeasure(bars.section('-', 1, 1).toInt() - 1);
            const engraving::staff_idx_t st = p->staves().front()->idx();
            ms->selection().setRangeTicks(m1->tick(), m2->endTick(), st, st + 1);
            ms->selection().updateSelectedElements();
            const ByteArray mime = ms->selection().mimeData();
            ms->deselectAll();
            static int n = 0;
            QFile f(autotestDir() + QString("/mime-%1.xml").arg(++n));
            f.open(QIODevice::WriteOnly);
            f.write(mime.constChar(), mime.size());
            autotestLog(QString("  wrote mime-%1.xml (%2 bytes)").arg(n).arg(mime.size()));
        }
    } else if (step.startsWith("newsection:")) {
        // newsection:<template key>: a section made as the StarScore panel makes it (the doubler as the song's), then
        // its parts, each new staff's swing at its first bar, and whether its chord-symbol track starts muted
        const QString key = step.mid(11);
        const int horns = starscore::hornSectionSize(key);
        const QString doubler = horns >= 3 ? songDoublerInstrumentId(horns) : QString();
        autotestLog("  doubler: " + (doubler.isEmpty() ? QString("(template's own)") : doubler));
        const RetVal<QString> made = createSectionFromTemplate(key, doubler);
        for (int i = 0; i < 30; ++i) {
            QCoreApplication::processEvents();
        }
        for (const StarScoreSection& sec : load().sections) {
            if (!made.ret || sec.id != made.val) {
                continue;
            }
            for (const QString& pid : sec.partIds) {
                const engraving::Part* p = masterScore()->partById(ID(pid));
                if (!p || p->staves().empty()) {
                    continue;
                }
                QStringList swings;
                for (engraving::Measure* mm = masterScore()->firstMeasure(); mm && swings.size() < 3; mm = mm->nextMeasure()) {
                    const engraving::SwingParameters sp = p->staves().front()->swing(mm->tick());
                    if (sp.isOn()) {
                        swings << QString("bar %1 ratio %2").arg(mm->no() + 1).arg(sp.swingRatio);
                    }
                }
                bool muted = false;
                if (INotationSoloMuteStatePtr sm = master->notation()->soloMuteState()) {
                    const engraving::InstrumentTrackId cid { p->id(), String(u"chord_symbols") };
                    muted = sm->trackSoloMuteStateExists(cid) && sm->trackSoloMuteState(cid).mute;
                }
                autotestLog(QString("  %1 (%2): swing %3; chords muted %4").arg(p->partName().toQString(), p->instrumentId().toQString(),
                                                                             swings.isEmpty() ? QString("none") : swings.join(", "))
                            .arg(muted ? "yes" : "no"));
            }
        }
        // the swing of an existing staff, for comparison
        if (!masterScore()->parts().empty()) {
            const engraving::SwingParameters sp = masterScore()->parts().front()->staves().front()->swing(masterScore()->lastMeasure()->tick());
            autotestLog(QString("  first staff swing at the end: %1").arg(sp.isOn() ? QString::number(sp.swingRatio) : QString("none")));
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
    } else if (step == "progresslist") {
        // progresslist: each colored bar as "<part> bar N: code"
        const auto map = engraving::starscore::parseProgress(masterScore()->metaTag(String(engraving::starscore::PROGRESS_TAG)).toStdString());
        QStringList out;
        for (engraving::Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
            if (!m->eid().isValid()) {
                continue;
            }
            for (const engraving::Part* p : ms->parts()) {
                for (size_t k = 0; k < p->nstaves(); ++k) {
                    auto it = map.find(engraving::starscore::progressKey(m->eid().toStdString(), p->id().toStdString(), k));
                    if (it != map.end()) {
                        out << QString("%1 bar %2: %3").arg(p->partName().toQString()).arg(m->no() + 1).arg(QChar(it->second));
                    }
                }
            }
        }
        autotestLog("  " + out.join(" | "));
    } else if (step.startsWith("barfermata:") || step.startsWith("fermatas:")) {
        // barfermata:S:B: a fermata on the end barline of bar B (1-based) on staff S (0-based), in the score on screen;
        // fermatas:S: every fermata on staff S: bar, and whether on a note/rest or a barline
        const QStringList a = step.split(':');
        INotationPtr n = globalContext()->currentNotation();
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        const engraving::staff_idx_t st = engraving::staff_idx_t(a.value(1).toInt());
        if (sc && step.startsWith("barfermata:")) {
            engraving::Measure* m = sc->crMeasure(a.value(2).toInt() - 1);
            if (m) {
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Autotest fermata"));
                engraving::Segment* seg = m->undoGetSegment(engraving::SegmentType::EndBarLine, m->endTick());
                engraving::Fermata* f = engraving::Factory::createFermata(seg);
                f->setTrack(engraving::staff2track(st));
                f->setParent(seg);
                sc->undoAddElement(f);
                n->undoStack()->commitChanges();
                autotestLog("  added");
            }
        } else if (sc) {
            QStringList out;
            for (engraving::Segment* seg = sc->firstSegment(engraving::SegmentType::All); seg; seg = seg->next1()) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    if (e->isFermata() && e->staffIdx() == st) {
                        out << QString("bar %1 %2").arg(seg->measure()->no() + 1)
                               .arg(seg->isType(engraving::SegmentType::BarLineType) ? "barline" : "note");
                    }
                }
            }
            autotestLog("  fermatas: " + out.join(", "));
        }
    } else if (step == "restclashes") {
        // restclashes: in the main score and every part score as laid out on screen, the beamed rests on a note or an
        // accidental (a grace note's too)
        std::vector<std::pair<QString, engraving::Score*> > all { { QString("(main score)"), ms } };
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->notation()) {
                all.push_back({ e->name(), e->notation()->elements()->msScore() });
            }
        }
        int total = 0;
        for (auto& [name, sc] : all) {
            sc->setLayoutAll();
            sc->doLayout();
            QStringList found;
            for (const starscore::LayoutIssue& i : starscore::layoutIssues(sc)) {
                if (i.kind == "rest on a note") {
                    found << QString("b%1 %2").arg(i.bar).arg(i.what);
                }
            }
            if (!found.isEmpty()) {
                autotestLog("  " + name + ": " + found.join(", "));
                total += int(found.size());
            }
        }
        autotestLog(QString("  %1 in all").arg(total));
    } else if (step.startsWith("makerhythm:")) {
        // makerhythm:<staff>:<bar>:above|below|normal: the notes of that staff's bar selected and made rhythm markings
        // (makerhythm:<staff in that score>:<bar>:above|below:<part score name>: in that part score)
        const QStringList a = step.split(':');
        engraving::Score* sc = ms;
        INotationPtr where = master->notation();
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (!step.section(':', 4).isEmpty() && e->name() == step.section(':', 4) && e->notation()) {
                where = e->notation();
                sc = where->elements()->msScore();
            }
        }
        engraving::Measure* m = sc->crMeasure(a.value(2).toInt() - 1);
        sc->deselectAll();
        if (m && a.value(5) == "rests") {
            // (:rests after the part score name: only the bar's rests selected, one by one)
            for (engraving::Segment* sg = m->first(engraving::SegmentType::ChordRest); sg; sg = sg->next(engraving::SegmentType::ChordRest)) {
                engraving::EngravingItem* e = sg->element(a.value(1).toInt() * engraving::VOICES);
                if (e && e->isRest()) {
                    sc->select(e, engraving::SelectType::ADD);
                }
            }
        } else if (m) {
            sc->selection().setRange(m->first(engraving::SegmentType::ChordRest), m->last(), a.value(1).toInt(), a.value(1).toInt() + 1);
            sc->selection().updateSelectedElements();
        }
        where->undoStack()->prepareChanges(TranslatableString::untranslatable("Rhythm markings"));
        // (normal: the markings made normal notes again)
        const int made = a.value(3) == "normal" ? starscore::unmakeRhythmMarkings(sc)
                         : starscore::makeRhythmMarkings(sc, a.value(3) == "below");
        where->undoStack()->commitChanges();
        sc->deselectAll();
        autotestLog(QString("  %1 note(s)").arg(made));
    } else if (step.startsWith("rhythmmarks:")) {
        // rhythmmarks:<bar>-<bar>: per staff, the rhythm markings in those bars (pinned or not) and the lines they show on
        ms->doLayout();
        const int b1 = step.mid(12).section('-', 0, 0).toInt(), b2 = step.mid(12).section('-', 1, 1).toInt();
        std::map<size_t, std::map<int, int> > lines;
        std::map<size_t, int> total, pinned;
        for (engraving::Measure* m = ms->crMeasure(b1 - 1); m && m->no() <= b2 - 1; m = m->nextMeasure()) {
            for (engraving::Segment* sg = m->first(engraving::SegmentType::ChordRest); sg; sg = sg->next(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* e : sg->elist()) {
                    if (!e || !e->isChord()) {
                        continue;
                    }
                    for (engraving::Note* n : engraving::toChord(e)->notes()) {
                        if (starscore::isRhythmMarking(n)) {
                            ++total[n->staffIdx()];
                            pinned[n->staffIdx()] += n->fixed() ? 1 : 0;
                            ++lines[n->staffIdx()][n->line()];
                        }
                    }
                }
            }
        }
        for (const auto& [si, count] : total) {
            QStringList l;
            for (const auto& [line, k] : lines[si]) {
                l << QString("line %1 x%2").arg(line).arg(k);
            }
            autotestLog(QString("  staff %1 %2: %3 markings, %4 pinned; %5").arg(si).arg(ms->staff(si)->part()->partName().toQString())
                        .arg(count).arg(pinned[si]).arg(l.join(", ")));
        }
    } else if (step.startsWith("copyrange:")) {
        // copyrange:S:B1-B2:T:C: staff S (0-based) bars B1..B2 copied and pasted on staff T at bar C, as Copy and Paste
        // do (the score on screen)
        const QStringList a = step.split(':');
        INotationPtr n = globalContext()->currentNotation();
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        if (a.size() == 5 && sc) {
            const int st = a[1].toInt(), b1 = a[2].section('-', 0, 0).toInt(), b2 = a[2].section('-', 1, 1).toInt();
            const int tt = a[3].toInt(), c = a[4].toInt();
            engraving::Measure* m1 = sc->crMeasure(b1 - 1);
            engraving::Measure* m2 = sc->crMeasure(b2 - 1);
            engraving::Measure* mc = sc->crMeasure(c - 1);
            if (m1 && m2 && mc) {
                sc->selection().setRangeTicks(m1->tick(), m2->endTick(), engraving::staff_idx_t(st), engraving::staff_idx_t(st + 1));
                sc->selection().updateSelectedElements();
                n->interaction()->copySelection();
                sc->deselectAll();
                if (a[3].contains('-')) {
                    // a range of staves as the target (one staff copied onto several)
                    const int t1 = a[3].section('-', 0, 0).toInt(), t2 = a[3].section('-', 1, 1).toInt();
                    engraving::Measure* me = sc->crMeasure(c - 1 + (b2 - b1));
                    sc->selection().setRangeTicks(mc->tick(), (me ? me : mc)->endTick(), engraving::staff_idx_t(t1), engraving::staff_idx_t(t2 + 1));
                    sc->selection().updateSelectedElements();
                } else {
                    n->interaction()->select({ mc->first(engraving::SegmentType::ChordRest)->element(engraving::staff2track(engraving::staff_idx_t(tt))) },
                                             SelectType::SINGLE);
                }
                n->interaction()->pasteSelection();
                autotestLog("  pasted");
            }
        }
        const QString tag = masterScore()->metaTag(String(engraving::starscore::PROGRESS_TAG)).toQString();
        autotestLog(QString("  %1 marks").arg(tag.isEmpty() ? 0 : tag.count(';') + 1));
    } else if (step == "bookaudit" || step == "bookaudit:fix") {
        // bookaudit[:fix]: part scores that hold more than they should. A part score that isn't an arrangement's
        // score holds one instrument: extra instruments in it (an instrument added to it by mistake) are listed and,
        // with :fix, taken out of that part score only; empty part scores and second part scores of the same
        // instrument (same name) are listed and removed.
        const bool fix = step.endsWith(":fix");
        std::set<QString> arrangementScores;
        for (const StarScoreArrangement& a : load().arrangements) {
            if (!a.scoreName.isEmpty()) {
                arrangementScores.insert(a.scoreName);
            }
        }
        QStringList issues;
        ExcerptNotationList kept;
        std::set<std::pair<QString, QString> > seen;   // (name, part id) of single-part scores
        std::vector<std::pair<INotationPtr, std::vector<muse::ID> > > strays;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es) {
                kept.push_back(e);
                continue;
            }
            const QString name = e->name();
            if (es->parts().empty()) {
                issues << QString("\"%1\": empty part score%2").arg(name, fix ? " (removed)" : "");
                if (!fix) {
                    kept.push_back(e);
                }
                continue;
            }
            if (arrangementScores.count(name) || es->parts().size() == 1) {
                if (es->parts().size() == 1) {
                    const engraving::Part* mp = masterPartsOf(es, ms).empty() ? nullptr : masterPartsOf(es, ms).front();
                    const auto key = std::make_pair(name, mp ? idText(mp) : QString());
                    if (mp && seen.count(key)) {
                        issues << QString("\"%1\": a second identical part score of %2%3").arg(name, mp->partName().toQString(), fix ? " (removed)" : "");
                        if (!fix) {
                            kept.push_back(e);
                        }
                        continue;
                    }
                    seen.insert(key);
                }
                kept.push_back(e);
                continue;
            }
            // which instrument is the part score's own: the one it is named after, else the one it was made for
            const std::vector<engraving::Part*> mps = masterPartsOf(es, ms);
            int own = -1;
            for (size_t i = 0; i < es->parts().size(); ++i) {
                if (es->parts()[i]->partName().toQString() == name || (i < mps.size() && mps[i] && mps[i]->partName().toQString() == name)) {
                    own = int(i);
                }
            }
            QStringList names;
            for (const engraving::Part* p : es->parts()) {
                names << p->partName().toQString();
            }
            if (own < 0) {
                issues << QString("\"%1\": holds %2; not an arrangement score and not named after one of them (left as it is)")
                          .arg(name, names.join(", "));
                kept.push_back(e);
                continue;
            }
            std::vector<muse::ID> extra;
            QStringList extraNames;
            for (size_t i = 0; i < es->parts().size(); ++i) {
                if (int(i) != own) {
                    extra.push_back(es->parts()[i]->id());
                    extraNames << es->parts()[i]->partName().toQString();
                }
            }
            issues << QString("\"%1\": also holds %2%3").arg(name, extraNames.join(", "), fix ? " (taken out of this part score)" : "");
            strays.emplace_back(n, extra);
            kept.push_back(e);
        }
        if (fix) {
            for (auto& [n, ids] : strays) {
                n->parts()->removeParts(ids);
            }
            if (kept.size() != master->excerpts().size()) {
                master->setExcerpts(kept);
            }
        }
        autotestLog(QString("  %1 issue(s)%2").arg(issues.size()).arg(issues.isEmpty() ? QString() : "\n    - " + issues.join("\n    - ")));
    } else if (step.startsWith("clearpart:")) {
        // clearpart:<part name>: every bar of the part's staves emptied (rests), one undo step
        const QString name = step.mid(10);
        for (engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() != name || p->staves().empty()) {
                continue;
            }
            const engraving::staff_idx_t s1 = p->staves().front()->idx();
            const engraving::staff_idx_t s2 = p->staves().back()->idx() + 1;
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Clear part"));
            ms->selection().setRangeTicks(engraving::Fraction(0, 1), ms->lastMeasure()->endTick(), s1, s2);
            ms->selection().updateSelectedElements();
            ms->cmdDeleteSelection();
            ms->deselectAll();
            master->notation()->undoStack()->commitChanges();
            autotestLog(QString("  cleared %1 (%2 staves)").arg(name).arg(s2 - s1));
        }
    } else if (step.startsWith("partbook:")) {
        // partbook:<part name>[|<part score name>]: a part score of its own for the part (named as given)
        const QString name = step.mid(9).section('|', 0, 0), book = step.mid(9).section('|', 1);
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == name) {
                addPartBooksFor({ idText(p) });
                if (!book.isEmpty()) {
                    for (const IExcerptNotationPtr& e : master->excerpts()) {
                        if (e->name() == name) {
                            e->setName(book);
                        }
                    }
                }
            }
        }
        QStringList books;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            books << e->name();
        }
        autotestLog("  part scores: " + books.join(" | "));
    } else if (step.startsWith("setmutes:")) {
        // setmutes:<part name>|<mute>;<mute>: the part's mutes, then every planned sheet title that shows mutes
        const QString name = step.mid(9).section('|', 0, 0);
        const QStringList mutes = step.mid(9).section('|', 1).split(';', Qt::SkipEmptyParts);
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == name) {
                setPartMutes(idText(p), mutes);
            }
        }
        const RetVal<StarScoreBandExportPlan> plan = planBandExport();
        for (const StarScoreBandFile& f : plan.val.files) {
            if (f.sheetLeft.contains("\n")) {
                autotestLog(QString("  %1: %2").arg(f.relativePath, QString(f.sheetLeft).replace("\n", " / ")));
            }
        }
    } else if (step.startsWith("flugel:")) {
        // flugel:<part name>[|<part name>…]: mark those parts as played on flugelhorn, then every planned sheet's file
        // and its title top left (with #<filter> after the names, only paths containing it)
        const QStringList names = step.mid(7).section('#', 0, 0).split('|', Qt::SkipEmptyParts);
        const QString filter = step.mid(7).section('#', 1);
        if (!qEnvironmentVariable("STARSCORE_AUTOTEST_BAND").isEmpty()) {
            setBandFolder(qEnvironmentVariable("STARSCORE_AUTOTEST_BAND"));
        }
        Data d = load();
        for (const engraving::Part* p : ms->parts()) {
            if (names.contains(p->partName().toQString())) {
                d.flugelhornParts.insert(idText(p));
            }
        }
        store(d);
        labelPartBooks();
        const RetVal<StarScoreBandExportPlan> plan = planBandExport();
        if (!plan.ret) {
            autotestLog("  no plan: " + QString::fromStdString(plan.ret.toString()));
        }
        for (const StarScoreBandFile& f : plan.val.files) {
            if (!f.isScore && (filter.isEmpty() || f.relativePath.contains(filter))) {
                autotestLog(QString("  %1: %2").arg(f.relativePath, QString(f.sheetLeft).replace("\n", " / ")));
            }
        }
    } else if (step == "basses") {
        // the bass guitar parts (name, long name, strings, notes), their part scores and sections
        for (const engraving::Part* p : ms->parts()) {
            const QString id = p->instrumentId().toQString();
            if (!id.contains("bass") || id.contains("clarinet") || id.contains("trombone") || id.contains("sax") || id.contains("soon")) {
                continue;
            }
            QStringList books, secs;
            for (const IExcerptNotationPtr& e : master->excerpts()) {
                engraving::Score* es = e->notation() ? e->notation()->elements()->msScore() : nullptr;
                if (es && std::any_of(es->parts().begin(), es->parts().end(), [&](const engraving::Part* bp) {
                    return std::any_of(bp->staves().begin(), bp->staves().end(), [&](const engraving::Staff* st) {
                        const engraving::Staff* l = st->findLinkedInScore(ms);
                        return l && l->part() == p;
                    });
                })) {
                    books << e->name();
                }
            }
            for (const StarScoreSection& sec : load().sections) {
                if (sec.partIds.contains(idText(p))) {
                    secs << sec.name;
                }
            }
            autotestLog(QString("  %1 [%2] long \"%3\" strings %4 notes %5 | books: %6 | sections: %7")
                        .arg(p->partName().toQString(), id, p->longName().toQString())
                        .arg(p->instrument()->stringData() ? int(p->instrument()->stringData()->strings()) : 0)
                        .arg(starscore::partHasNotes(p) ? "yes" : "no", books.join(", "), secs.join(", ")));
        }
    } else if (step.startsWith("addtextat:")) {
        // addtextat:<part name>|<bar>|<text>: a staff text on the part's first beat of that bar (in the main score)
        const QStringList a = step.mid(10).split('|');
        engraving::Measure* m = ms->firstMeasure();
        for (int i = 1; i < a.value(1).toInt() && m; ++i) {
            m = m->nextMeasure();
        }
        engraving::Segment* seg = m ? m->first(engraving::SegmentType::ChordRest) : nullptr;
        int added = 0;
        for (engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() != a.value(0) || !seg) {
                continue;
            }
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Staff text"));
            engraving::StaffText* t = engraving::Factory::createStaffText(seg);
            t->setTrack(p->startTrack());
            t->setParent(seg);
            t->setXmlText(a.value(2));
            ms->undoAddElement(t);
            master->notation()->undoStack()->commitChanges();
            ++added;
        }
        autotestLog(QString("  added %1").arg(added));
    } else if (step.startsWith("parenchords:")) {
        // parenchords:<bar>: parentheses on every chord symbol in that bar of the main score, as the Properties panel
        // adds them (to the linked chord symbols in the part scores too)
        engraving::Measure* m = ms->firstMeasure();
        for (int i = 1; i < step.mid(12).toInt() && m; ++i) {
            m = m->nextMeasure();
        }
        int added = 0;
        for (engraving::Segment* seg = m ? m->first() : nullptr; seg; seg = seg->next()) {
            for (engraving::EngravingItem* a : seg->annotations()) {
                if (!a->isHarmony()) {
                    continue;
                }
                master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Parentheses"));
                a->undoChangeProperty(engraving::Pid::HAS_PARENTHESES, engraving::ParenthesesMode::BOTH);
                master->notation()->undoStack()->commitChanges();
                ++added;
            }
        }
        autotestLog(QString("  chord symbols %1").arg(added));
    } else if (step == "parentracks") {
        // parentracks: every parenthesis whose staff isn't its item's (or isn't in its score)
        int all = 0, wrong = 0;
        std::vector<engraving::Score*> scores { ms };
        for (engraving::Excerpt* ex : ms->excerpts()) {
            if (ex->excerptScore()) {
                scores.push_back(ex->excerptScore());
            }
        }
        for (engraving::Score* sc : scores) {
            sc->scanElements([&](engraving::EngravingItem* e) {
                if (!e->isParenthesis()) {
                    return;
                }
                ++all;
                const engraving::EngravingItem* parent = e->parentItem();
                if (e->track() >= sc->ntracks() || !e->staff() || (parent && parent->track() != e->track())) {
                    ++wrong;
                    autotestLog(QString("  %1: parenthesis track %2, its %3's %4, score tracks %5").arg(
                                    sc->isMaster() ? QString("main score") : sc->name().toQString()).arg(e->track())
                                .arg(parent ? parent->typeName() : "-").arg(parent ? int(parent->track()) : -1).arg(sc->ntracks()));
                }
            });
        }
        autotestLog(QString("  parentheses %1, wrong %2").arg(all).arg(wrong));
    } else if (step.startsWith("recordingshtml:")) {
        // recordingshtml:<recordings.json>|<code>|<song name>: that song's Recordings page as HTML, in recordings-<code>.html
        const QStringList a = step.mid(15).split('|');
        QFile f(a.value(0));
        mu::project::starscore::org::RecordingsData rec;
        if (f.open(QIODevice::ReadOnly)) {
            rec.load(QJsonDocument::fromJson(f.readAll()).object());
        }
        mu::project::starscore::org::SongInfo song;
        song.code = a.value(1);
        song.name = a.value(2);
        QFile out(autotestDir() + "/recordings-" + song.code + ".html");
        if (out.open(QIODevice::WriteOnly)) {
            out.write(mu::project::starscore::org::recordingsHtml(rec, song, QDate::currentDate()).toUtf8());
        }
        autotestLog(QString("  %1 performances, written %2").arg(rec.performancesOf(song.code).size()).arg(out.fileName()));
    } else if (step.startsWith("deltext:")) {
        // deltext:<part name>|<text>: removes the staff texts reading exactly that from the part (and its part scores)
        const QString name = step.mid(8).section('|', 0, 0), text = step.mid(8).section('|', 1);
        std::vector<engraving::EngravingItem*> gone;
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() != name) {
                continue;
            }
            for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
                 seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    if (e->isStaffText() && e->track() >= p->startTrack() && e->track() < p->endTrack()
                        && toTextBase(e)->plainText().toQString().trimmed() == text) {
                        gone.push_back(e);
                    }
                }
            }
        }
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Remove text"));
        for (engraving::EngravingItem* e : gone) {
            ms->undoRemoveElement(e);
        }
        master->notation()->undoStack()->commitChanges();
        autotestLog(QString("  removed %1").arg(gone.size()));
    } else if (step.startsWith("bookpdf:")) {
        // bookpdf:<part score name>: that part score as it is now, written to <part score name>.pdf (laid out first)
        const QString name = step.mid(8);
        int written = 0;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() != name || !e->notation()) {
                continue;
            }
            engraving::Score* es = e->notation()->elements()->msScore();
            es->setLayoutAll();
            es->doLayout();
            io::Buffer out;
            out.open(io::IODevice::WriteOnly);
            INotationWriter::Options options { { INotationWriter::OptionKey::UNIT_TYPE, Val(INotationWriter::UnitType::PER_PART) } };
            if (writers()->writer("pdf")->write(e->notation(), out, options)) {
                QFile f(autotestDir() + "/" + QString(name).replace('/', '-') + ".pdf");
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(out.data().toQByteArray());
                    ++written;
                }
            }
            out.close();
        }
        autotestLog(QString("  written %1").arg(written));
    } else if (step.startsWith("mutetext:")) {
        // mutetext:<text>|<text>…: whether each counts as a mute marking (hidden on woodwind sheets)
        for (const QString& t : step.mid(9).split('|')) {
            autotestLog(QString("  \"%1\": %2").arg(t, starscore::isMuteMarking(t) ? "mute" : "-"));
        }
    } else if (step.startsWith("booktitles")) {
        // booktitles[:<text in the part score name>]: each one-part part score's instrument name text (top left)
        const QString filter = step.section(':', 1);
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            engraving::Score* es = e->notation() ? e->notation()->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1 || (!filter.isEmpty() && !e->name().contains(filter))) {
                continue;
            }
            QString name;
            for (engraving::MeasureBase* mb = es->first(); mb && !mb->isMeasure(); mb = mb->next()) {
                if (!mb->isVBox()) {
                    continue;
                }
                QStringList texts;
                for (engraving::EngravingItem* t : mb->el()) {
                    if (t && t->isText()) {
                        texts << engraving::toTextBase(t)->plainText().toQString().replace("\n", " / ");
                    }
                }
                name = texts.join(" ‖ ");
                break;
            }
            autotestLog(QString("  %1: %2").arg(e->name(), name));
        }
    } else if (step == "partmutes") {
        const Data d = load();
        for (const auto& [pid, mutes] : d.partMutes) {
            const engraving::Part* p = ms->partById(ID(pid));
            autotestLog(QString("  %1: %2").arg(p ? p->partName().toQString() : pid, mutes.join("; ")));
        }
    } else if (step.startsWith("additive:")) {
        // additive:<bar>:<4+4+4+3>/<8>: the additive time signature from that bar (as the dialog does), then each bar's
        // time signature in short
        const int bar = step.section(':', 1, 1).section('-', 0, 0).toInt();
        const QString pattern = step.section(':', 2);
        std::vector<int> nums;
        for (const QString& n : pattern.section('/', 0, 0).split('+')) {
            nums.push_back(n.toInt());
        }
        engraving::Measure* first = ms->firstMeasure();
        for (int i = 1; i < bar && first; ++i) {
            first = first->nextMeasure();
        }
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Additive time signature"));
        // additive:<bar>-<last bar>:… : just that range
        engraving::Measure* lastBar = nullptr;
        if (step.section(':', 1, 1).contains('-')) {
            lastBar = ms->firstMeasure();
            for (int i = 1; i < step.section(':', 1, 1).section('-', 1).toInt() && lastBar; ++i) {
                lastBar = lastBar->nextMeasure();
            }
        }
        const QString err = starscore::applyAdditiveTimeSig(ms, first, lastBar, nums, pattern.section('/', 1).toInt());
        master->notation()->undoStack()->commitChanges();
        QStringList sigs;
        for (engraving::Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
            sigs << QString("%1/%2").arg(m->ticks().numerator()).arg(m->ticks().denominator());
        }
        autotestLog(QString("  %1 bars: %2%3").arg(sigs.size()).arg(sigs.join(" ")).arg(err.isEmpty() ? QString() : " — " + err));
    } else if (step.startsWith("mmrests:")) {
        // mmrests:<part score>: its multimeasure rests: first bar, bars held, the number shown
        INotationPtr n;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == step.mid(8) && e->notation()) {
                n = e->notation();
            }
        }
        engraving::Score* sc = step.mid(8) == "main" ? ms : n ? n->elements()->msScore() : nullptr;
        if (sc) {
            sc->doLayout();
            QStringList out;
            for (engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
                if (engraving::Measure* mm = m->mmRest()) {
                    int shown = -1;
                    for (engraving::Segment* sg = mm->first(engraving::SegmentType::ChordRest); sg; sg = sg->next(engraving::SegmentType::ChordRest)) {
                        if (sg->element(0) && sg->element(0)->isMMRest()) {
                            shown = engraving::toMMRest(sg->element(0))->ldata()->number;
                        }
                    }
                    out << QString("at %1 (bar %2): %3 bars, shows %4").arg(m->tick().toString().toQString()).arg(m->no() + 1).arg(mm->mmRestCount()).arg(shown);
                }
            }
            autotestLog("  " + out.join("; "));
            QStringList sigs;
            int bar = 0;
            for (engraving::Measure* m = sc->firstMeasure(); m && bar < 16; m = m->nextMeasure(), ++bar) {
                engraving::Segment* sg = m->findSegmentR(engraving::SegmentType::TimeSig, engraving::Fraction(0, 1));
                engraving::EngravingItem* e = sg ? sg->element(0) : nullptr;
                sigs << (e && e->isTimeSig() ? QString("%1%2").arg(engraving::toTimeSig(e)->numeratorString().toQString())
                         .arg(e->visible() ? "" : "h") : QString("-"));
            }
            autotestLog("  time signatures: " + sigs.join(" "));
        }
    } else if (step.startsWith("deltimesigs:")) {
        // deltimesigs:<bar>+<bar>+…: those bars' time signatures selected together (in that order) and deleted with one
        // Delete, then each bar's length
        ms->deselectAll();
        for (const QString& b : step.mid(12).split('+')) {
            engraving::Measure* m = ms->firstMeasure();
            for (int i = 1; i < b.toInt() && m; ++i) {
                m = m->nextMeasure();
            }
            engraving::Segment* seg = m ? m->findSegment(engraving::SegmentType::TimeSig, m->tick()) : nullptr;
            if (seg && seg->element(0)) {
                ms->select(seg->element(0), engraving::SelectType::ADD, 0);
            }
        }
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Delete"));
        ms->cmdDeleteSelection();
        master->notation()->undoStack()->commitChanges();
        QStringList sigs;
        for (engraving::Measure* mm = ms->firstMeasure(); mm; mm = mm->nextMeasure()) {
            sigs << QString("%1/%2").arg(mm->ticks().numerator()).arg(mm->ticks().denominator());
        }
        autotestLog(QString("  %1 bars: %2").arg(sigs.size()).arg(sigs.join(" ")));
    } else if (step.startsWith("deltimesig:")) {
        // deltimesig:<bar>: that bar's time signature deleted (as selecting it and pressing Delete), then each bar's
        // length
        engraving::Measure* m = ms->firstMeasure();
        for (int i = 1; i < step.mid(11).toInt() && m; ++i) {
            m = m->nextMeasure();
        }
        engraving::Segment* seg = m ? m->findSegment(engraving::SegmentType::TimeSig, m->tick()) : nullptr;
        engraving::EngravingItem* e = seg ? seg->element(0) : nullptr;
        if (e && e->isTimeSig()) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Delete time signature"));
            ms->cmdRemoveTimeSig(engraving::toTimeSig(e));
            master->notation()->undoStack()->commitChanges();
        }
        QStringList sigs;
        int hidden = 0;
        for (engraving::Measure* mm = ms->firstMeasure(); mm; mm = mm->nextMeasure()) {
            sigs << QString("%1/%2").arg(mm->ticks().numerator()).arg(mm->ticks().denominator());
            engraving::Segment* s2 = mm->findSegment(engraving::SegmentType::TimeSig, mm->tick());
            hidden += s2 && s2->element(0) && !s2->element(0)->visible() ? 1 : 0;
        }
        int notes = 0, tied = 0;
        for (engraving::Segment* sg = ms->firstSegment(engraving::SegmentType::ChordRest); sg; sg = sg->next1(engraving::SegmentType::ChordRest)) {
            for (engraving::EngravingItem* e : sg->elist()) {
                if (e && e->isChord()) {
                    for (const engraving::Note* n : engraving::toChord(e)->notes()) {
                        ++notes;
                        tied += n->tieFor() ? 1 : 0;
                    }
                }
            }
        }
        autotestLog(QString("  %1 bars, %2 hidden time signatures, %3 notes (%4 tied on): %5").arg(sigs.size()).arg(hidden)
                    .arg(notes).arg(tied).arg(sigs.join(" ")));
    } else if (step == "makekeys") {
        autotestLog("  " + makeKeysPart());
        QStringList books;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            books << e->name();
        }
        autotestLog("  part scores: " + books.join(" | "));
    } else if (step.startsWith("bookadd:")) {
        // bookadd:<part score name>|<part name>[|<new part score name>]: the main score's part added to the part score
        // (as ticking its eye in the part score's Instruments panel does), and the part score renamed
        const QStringList a = step.mid(8).split('|');
        IExcerptNotationPtr book;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == a.value(0)) {
                book = e;
            }
        }
        const engraving::Part* add = nullptr;
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == a.value(1)) {
                add = p;
            }
        }
        if (!book || !book->notation() || !add) {
            autotestLog("  part score or part not found");
        } else {
            INotationPartsPtr parts = book->notation()->parts();
            parts->insertPart(add->clone(), book->notation()->elements()->msScore()->parts().size());
            if (!a.value(2).isEmpty()) {
                book->setName(a.value(2));
            }
            QStringList names;
            for (const engraving::Part* p : masterPartsOf(book->notation()->elements()->msScore(), ms)) {
                names << p->partName().toQString();
            }
            autotestLog(QString("  %1: %2").arg(book->name(), names.join(", ")));
        }
    } else if (step.startsWith("addinstrument:")) {
        // addinstrument:<instrument id>|<part name>|<section id>|<after part name>: a new instrument in the section,
        // placed after the named part, with its own part score
        const QStringList a = step.mid(14).split('|');
        const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(a.value(0)));
        if (tpl.id.isEmpty()) {
            autotestLog("  unknown instrument " + a.value(0));
        } else {
            std::set<QString> before;
            PartInstrumentList list;
            for (const engraving::Part* p : ms->parts()) {
                before.insert(idText(p));
                PartInstrument pi;
                pi.isExistingPart = true;
                pi.partId = p->id();
                list << pi;
                if (p->partName().toQString() == a.value(3)) {
                    PartInstrument ni;
                    ni.isExistingPart = false;
                    ni.instrumentTemplate = tpl;
                    list << ni;
                }
            }
            engraving::ScoreOrder order = master->parts()->scoreOrder();
            order.customized = true;
            master->parts()->setParts(list, order);
            std::vector<engraving::Part*> made;
            for (engraving::Part* p : ms->parts()) {
                if (!before.count(idText(p))) {
                    made.push_back(p);
                }
            }
            StarScoreInstrument inst;
            inst.instrumentId = a.value(0);
            inst.partName = a.value(1);
            inst.hidden = a.value(4) == "hidden";   // (|hidden after the part it follows: as the template's Congas)
            const StarScoreSection sec = finishNewParts(made, { inst });
            Data d = load();
            for (StarScoreSection& s : d.sections) {
                if (s.id == a.value(2)) {
                    for (const QString& pid : sec.partIds) {
                        s.partIds << pid;
                        if (!s.shownPartIds.isEmpty()) {
                            s.shownPartIds << pid;
                        }
                    }
                }
            }
            store(d);
            autotestLog(QString("  added %1 (%2) to %3").arg(a.value(1), sec.partIds.join(","), a.value(2)));
        }
    } else if (step.startsWith("restoresection:")) {
        // restoresection:<file>|<section id>|<after part name>: a section of another copy of the song brought back:
        // its instruments (same instrument, name and short name) placed after the named part, their music pasted bar
        // for bar (the two must have the same bars), part scores for the parts that had one there, the section and
        // the parts' status tags (Joel, 21 Oct 2026: Double Entendre's Rhythm Section deleted by mistake)
        const QStringList a = step.mid(15).split('|');
        INotationProjectPtr src = projectCreator()->newProject(iocContext());
        if (!src->load(io::path_t(a.value(0))) || !src->masterNotation()) {
            autotestLog("  couldn't load " + a.value(0));
        } else {
            engraving::MasterScore* ss = src->masterNotation()->masterScore();
            const Data srcData = loadFrom(ss);
            const StarScoreSection* srcSec = nullptr;
            for (const StarScoreSection& sec : srcData.sections) {
                if (sec.id == a.value(1)) {
                    srcSec = &sec;
                }
            }
            int bars = 0, same = 0;
            for (engraving::Measure *x = ms->firstMeasure(), *y = ss->firstMeasure(); x || y;
                 x = x ? x->nextMeasure() : nullptr, y = y ? y->nextMeasure() : nullptr) {
                ++bars;
                same += (x && y && x->tick() == y->tick() && x->ticks() == y->ticks()) ? 1 : 0;
            }
            autotestLog(QString("  bars: %1, the same in both: %2").arg(bars).arg(same));
            if (!srcSec || same != bars) {
                autotestLog("  not restored");
            } else {
                // the instruments
                std::vector<engraving::Part*> srcParts;
                for (const QString& pid : srcSec->partIds) {
                    if (engraving::Part* p = ss->partById(ID(pid))) {
                        srcParts.push_back(p);
                    }
                }
                std::set<QString> before;
                PartInstrumentList list;
                for (const engraving::Part* p : ms->parts()) {
                    before.insert(idText(p));
                    PartInstrument pi;
                    pi.isExistingPart = true;
                    pi.partId = p->id();
                    list << pi;
                    if (p->partName().toQString() == a.value(2)) {
                        for (const engraving::Part* sp : srcParts) {
                            PartInstrument ni;
                            ni.isExistingPart = false;
                            ni.instrumentTemplate = instrumentsRepository()->instrumentTemplate(sp->instrumentId());
                            list << ni;
                        }
                    }
                }
                engraving::ScoreOrder order = master->parts()->scoreOrder();
                order.customized = true;
                master->parts()->setParts(list, order);
                std::vector<engraving::Part*> made;
                for (engraving::Part* p : ms->parts()) {
                    if (!before.count(idText(p))) {
                        made.push_back(p);
                    }
                }
                if (made.size() != srcParts.size()) {
                    autotestLog(QString("  made %1 parts for %2").arg(made.size()).arg(srcParts.size()));
                } else {
                    std::vector<StarScoreInstrument> insts;
                    for (const engraving::Part* sp : srcParts) {
                        StarScoreInstrument inst;
                        inst.instrumentId = sp->instrumentId().toQString();
                        inst.partName = sp->partName().toQString();
                        inst.shortName = sp->shortName(engraving::Fraction(0, 1)).toQString();
                        insts.push_back(inst);
                    }
                    StarScoreSection sec = finishNewParts(made, insts);
                    // the music, staff by staff
                    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Restore the section's music"));
                    const bool mm = ms->style().styleB(engraving::Sid::createMultiMeasureRests);
                    if (mm) {
                        ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, false);
                    }
                    ss->style().set(engraving::Sid::createMultiMeasureRests, false);
                    ss->setLayoutAll();
                    ss->doLayout();
                    ms->setLayoutAll();
                    ms->doLayout();
                    std::map<QString, int> keep;
                    for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
                         seg = seg->next1(engraving::SegmentType::ChordRest)) {
                        for (engraving::EngravingItem* e : seg->annotations()) {
                            if (e->systemFlag() || e->isRehearsalMark() || e->isTempoText()) {
                                keep[QString("%1|%2|%3").arg(int(e->type())).arg(seg->tick().ticks()).arg(e->track())]++;
                            }
                        }
                    }
                    for (size_t i = 0; i < made.size(); ++i) {
                        for (size_t k = 0; k < srcParts[i]->staves().size() && k < made[i]->staves().size(); ++k) {
                            const engraving::staff_idx_t from = srcParts[i]->staves().at(k)->idx();
                            const engraving::staff_idx_t to = made[i]->staves().at(k)->idx();
                            ss->deselectAll();
                            ss->selection().setRangeTicks(engraving::Fraction(0, 1), ss->lastMeasure()->endTick(), from, from + 1);
                            ss->selection().updateSelectedElements();
                            const ByteArray mime = ss->selection().mimeData();
                            ss->deselectAll();
                            engraving::XmlReader reader(mime);
                            const bool ok = !mime.empty()
                                            && ms->pasteStaff(reader, ms->firstMeasure()->first(engraving::SegmentType::ChordRest), to);
                            ms->deselectAll();
                            autotestLog(QString("  %1 staff %2: %3").arg(made[i]->partName().toQString()).arg(k + 1)
                                        .arg(ok ? "pasted" : "FAILED"));
                        }
                    }
                    // system markings the paste brought a second time
                    std::map<QString, int> seen;
                    std::vector<engraving::EngravingItem*> extra;
                    for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
                         seg = seg->next1(engraving::SegmentType::ChordRest)) {
                        for (engraving::EngravingItem* e : seg->annotations()) {
                            if (e->systemFlag() || e->isRehearsalMark() || e->isTempoText()) {
                                const QString key = QString("%1|%2|%3").arg(int(e->type())).arg(seg->tick().ticks()).arg(e->track());
                                if (++seen[key] > std::max(1, keep[key])) {
                                    extra.push_back(e);
                                }
                            }
                        }
                    }
                    for (engraving::EngravingItem* e : extra) {
                        ms->undoRemoveElement(e);
                    }
                    if (mm) {
                        ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, true);
                    }
                    master->notation()->undoStack()->commitChanges();
                    autotestLog(QString("  removed %1 system marking(s) brought twice").arg(extra.size()));
                    // the section, after the one holding the part it follows, and the status tags
                    Data d = load();
                    StarScoreSection restored = *srcSec;
                    std::map<QString, QString> newId;
                    for (size_t i = 0; i < made.size(); ++i) {
                        newId[idText(srcParts[i])] = idText(made[i]);
                    }
                    auto mapIds = [&](const QStringList& ids) {
                        QStringList out;
                        for (const QString& id : ids) {
                            if (newId.count(id)) {
                                out << newId[id];
                            }
                        }
                        return out;
                    };
                    restored.partIds = mapIds(srcSec->partIds);
                    restored.shownPartIds = mapIds(srcSec->shownPartIds);
                    restored.alternates.clear();
                    restored.sheetParts.clear();
                    size_t at = d.sections.size();
                    for (size_t i = 0; i < d.sections.size(); ++i) {
                        for (const QString& pid : d.sections[i].partIds) {
                            const engraving::Part* p = ms->partById(ID(pid));
                            if (p && p->partName().toQString() == a.value(2)) {
                                at = i + 1;
                            }
                        }
                    }
                    d.sections.insert(d.sections.begin() + std::min(at, d.sections.size()), restored);
                    int tags = 0;
                    for (const auto& [oldId, nid] : newId) {
                        d.partStatus.erase(nid);   // (new parts are tagged Sketch; they get the tag they had there, or none)
                        auto st = srcData.partStatus.find(oldId);
                        if (st != srcData.partStatus.end()) {
                            d.partStatus[nid] = st->second;
                            ++tags;
                        }
                    }
                    store(d);
                    autotestLog(QString("  section %1 with %2 part(s), %3 status tag(s)").arg(restored.name).arg(restored.partIds.size()).arg(tags));
                    // part scores only for the parts that had one of their own there
                    std::set<QString> hadBook;
                    for (engraving::Excerpt* ex : ss->excerpts()) {
                        engraving::Score* es = ex->excerptScore();
                        if (es && es->parts().size() == 1) {
                            if (const engraving::Staff* l = es->parts().front()->staff(0)->findLinkedInScore(ss)) {
                                hadBook.insert(idText(l->part()));
                            }
                        }
                    }
                    ExcerptNotationList kept;
                    int dropped = 0;
                    for (const IExcerptNotationPtr& e : master->excerpts()) {
                        INotationPtr n = e->notation();
                        engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
                        bool drop = false;
                        if (es && es->parts().size() == 1) {
                            const std::vector<engraving::Part*> mp = masterPartsOf(es, ms);
                            for (size_t i = 0; i < made.size(); ++i) {
                                if (mp.size() == 1 && mp.front() == made[i] && !hadBook.count(idText(srcParts[i]))) {
                                    drop = true;
                                }
                            }
                        }
                        if (drop) {
                            ++dropped;
                        } else {
                            kept.push_back(e);
                        }
                    }
                    if (dropped) {
                        master->setExcerpts(kept);
                    }
                    QStringList books;
                    for (const IExcerptNotationPtr& e : master->excerpts()) {
                        books << e->name();
                    }
                    autotestLog(QString("  part scores (%1 dropped): %2").arg(dropped).arg(books.join(", ")));
                    applyStyles(restored.partIds);
                }
            }
        }
    } else if (step.startsWith("sectionadd:")) {
        // sectionadd:<section id>|<part name>[|<part name>…]: parts already in the score put in the section (shown with
        // it), in score order
        const QStringList a = step.mid(11).split('|');
        Data d = load();
        QStringList added;
        for (StarScoreSection& sec : d.sections) {
            if (sec.id != a.value(0)) {
                continue;
            }
            for (const engraving::Part* p : ms->parts()) {
                const QString pid = idText(p);
                if (a.mid(1).contains(p->partName().toQString()) && !sec.partIds.contains(pid)) {
                    sec.partIds << pid;
                    if (!sec.shownPartIds.isEmpty() || p->show()) {
                        sec.shownPartIds << pid;
                    }
                    added << p->partName().toQString();
                }
            }
            // in score order
            QStringList ordered, shown;
            for (const engraving::Part* p : ms->parts()) {
                if (sec.partIds.contains(idText(p))) {
                    ordered << idText(p);
                }
                if (sec.shownPartIds.contains(idText(p))) {
                    shown << idText(p);
                }
            }
            sec.partIds = ordered;
            sec.shownPartIds = shown;
        }
        store(d);
        autotestLog(QString("  added %1").arg(added.join(", ")));
    } else if (step.startsWith("bookrename:")) {
        // bookrename:<part score>|<new name>
        const QStringList a = step.mid(11).split('|');
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == a.value(0)) {
                e->setName(a.value(1));
                autotestLog(QString("  renamed %1 to %2").arg(a.value(0), a.value(1)));
            }
        }
    } else if (step.startsWith("bookorder:")) {
        // bookorder:<part score>|<part score>|…: the part scores in this order (the tabs' order), any others after them
        // in the order they had
        const QStringList want = step.mid(10).split('|');
        ExcerptNotationList all = master->excerpts();
        ExcerptNotationList ordered;
        for (const QString& name : want) {
            for (const IExcerptNotationPtr& e : all) {
                if (e->name() == name) {
                    ordered.push_back(e);
                }
            }
        }
        for (const IExcerptNotationPtr& e : all) {
            if (!want.contains(e->name())) {
                ordered.push_back(e);
            }
        }
        master->setExcerpts(ordered);
        QStringList books;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            books << e->name();
        }
        autotestLog("  part scores: " + books.join(" | "));
    } else if (step.startsWith("arrsection:")) {
        // arrsection:<arrangement id>|<section id>: the section put first in the arrangement (as a Standard
        // arrangement's Lead Sheet), if it isn't in it
        const QStringList a = step.mid(11).split('|');
        Data d = load();
        for (StarScoreArrangement& arr : d.arrangements) {
            if (arr.id == a.value(0) && !arr.sectionIds.contains(a.value(1))) {
                arr.sectionIds.prepend(a.value(1));
                autotestLog(QString("  %1: %2").arg(arr.name, arr.sectionIds.join(", ")));
            }
        }
        store(d);
    } else if (step.startsWith("movetop:")) {
        // movetop:<part name>: the part to the top of the score, the system markings (rehearsal marks, tempo…) with it
        // onto its top staff, as dragging it to the top in the Instruments panel and answering Move
        engraving::Part* moving = nullptr;
        for (engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == step.mid(8)) {
                moving = p;
            }
        }
        engraving::Part* top = ms->parts().empty() ? nullptr : ms->parts().front();
        if (moving && top && moving != top) {
            const muse::ID oldTopStaff = top->staves().front()->id();
            master->parts()->moveParts({ moving->id() }, top->id(), INotationParts::InsertMode::Before);
            master->parts()->moveSystemObjects(oldTopStaff, moving->staves().front()->id());
            autotestLog(QString("  %1 moved to the top").arg(step.mid(8)));
        }
    } else if (step.startsWith("removepart:")) {
        // removepart:<words>: parts whose instrument's long name contains the words (case ignored) removed, as the
        // Instruments panel's delete does; then the part scores left
        const QString words = step.mid(11);
        muse::IDList ids;
        for (const engraving::Part* p : ms->parts()) {
            if (p->longName().toQString().contains(words, Qt::CaseInsensitive)) {
                ids.push_back(p->id());
                autotestLog(QString("  removing %1 (%2, %3)").arg(idText(p), p->partName().toQString(), p->longName().toQString()));
            }
        }
        if (!ids.empty()) {
            master->parts()->removeParts(ids);
            store(load());
        }
        // part scores left with no instrument (as deleting an instrument leaves its part score) taken out
        ExcerptNotationList kept;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            engraving::Score* es = e && e->notation() && e->notation()->elements() ? e->notation()->elements()->msScore() : nullptr;
            if (es && es->parts().empty()) {
                autotestLog("  empty part score removed: " + e->name());
            } else {
                kept.push_back(e);
            }
        }
        if (kept.size() != master->excerpts().size()) {
            master->setExcerpts(kept);
        }
        QStringList books;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            books << e->name();
        }
        autotestLog("  part scores: " + books.join(" | "));
    } else if (step.startsWith("horncopy:")) {
        // horncopy:<from section template key>:<to section template key>: the parts on matching instruments copied,
        // as "Copy the parts" does after a new horn arrangement (no question)
        const QString fromKey = step.section(':', 1, 1), toKey = step.section(':', 2, 2);
        QString fromId, toId;
        for (const StarScoreSection& s : load().sections) {
            fromId = s.templateKey == fromKey ? s.id : fromId;
            toId = s.templateKey == toKey ? s.id : toId;
        }
        const auto pairs = matchingHornParts(fromId, toId);
        for (const auto& [a, b] : pairs) {
            autotestLog("  " + partScoreName(a) + " -> " + partScoreName(b));
        }
        autotestLog(QString("  copied %1").arg(copyHornParts(pairs)));
        const auto st = partStatuses();
        for (const auto& [a, b] : pairs) {
            autotestLog(QString("  %1 status %2").arg(partScoreName(b)).arg(st.count(b) ? int(st.at(b)) : -1));
        }
    } else if (step.startsWith("copytexts:")) {
        // copytexts:<part score A>|<part score B>: copyTextPositions from A to B (one undo step); how many changed
        const QString an = step.mid(10).section('|', 0, 0), bn = step.mid(10).section('|', 1);
        INotationPtr A, B;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            A = e->name() == an ? e->notation() : A;
            B = e->name() == bn ? e->notation() : B;
        }
        if (A && B) {
            B->undoStack()->prepareChanges(TranslatableString::untranslatable("copy texts"));
            const int n = starscore::copyTextPositions(A->elements()->msScore(), B->elements()->msScore());
            B->undoStack()->commitChanges();
            autotestLog(QString("  %1 changed").arg(n));
        }
    } else if (step.startsWith("layoutinfo:") || step.startsWith("mmtoggle:")) {
        // layoutinfo:<part score>: its multimeasure rest setting, breaks and system locks (bar numbers);
        // mmtoggle:<part score>: the M key there (Toggle multimeasure rests), then its layout as layoutinfo
        const QString bn = step.mid(step.indexOf(':') + 1);
        INotationPtr n;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == bn && e->notation()) {
                n = e->notation();
            }
        }
        engraving::Score* sc = bn == "main" ? ms : n ? n->elements()->msScore() : nullptr;
        if (!sc) {
            autotestLog("  part score not found");
        } else {
            if (step.startsWith("mmtoggle:")) {
                INotationPtr target = bn == "main" ? master->notation() : n;
                target->undoStack()->prepareChanges(TranslatableString::untranslatable("Toggle multimeasure rests"));
                sc->cmdToggleMmrest();
                target->undoStack()->commitChanges();
                target->notationChanged().notify();
                sc->doLayout();
            }
            QStringList lines, pages, locks;
            for (const engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
                if (m->lineBreak()) {
                    lines << QString::number(m->no() + 1);
                }
                if (m->pageBreak()) {
                    pages << QString::number(m->no() + 1);
                }
            }
            for (const engraving::SystemLock* l : sc->systemLocks()->allLocks()) {
                locks << QString("%1-%2%3").arg(l->startMB()->no() + 1).arg(l->endMB()->no() + 1)
                    .arg(l->startMB()->isMeasure() && toMeasure(l->startMB())->isMMRest() ? "(mm)" : "");
            }
            autotestLog(QString("  mmrests %1; system breaks %2: %3; page breaks %4: %5; locks %6: %7")
                        .arg(sc->style().styleB(engraving::Sid::createMultiMeasureRests) ? "on" : "off")
                        .arg(lines.size()).arg(lines.join(",")).arg(pages.size()).arg(pages.join(","))
                        .arg(locks.size()).arg(locks.join(",")));
            const QString memory = sc->metaTag(u"starscoreLockMemory").toQString();
            if (!memory.isEmpty()) {
                autotestLog("  lock memory: " + QString(memory).replace('\n', " | "));
            }
        }
    } else if (step.startsWith("restsafteredit:")) {
        // restsafteredit:<part score>|<bar>: the first articulation in that bar moved a little (as dragging it would),
        // then each beamed rest's height and its beam's position after that partial layout, against a full layout
        // (Joel, 18 Oct 2026: Bumper Cars bar 11's rests out of line after moving something earlier on the page)
        const QString bn = step.mid(15).section('|', 0, 0);
        const int bar = step.mid(15).section('|', 1).toInt();
        INotationPtr n;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == bn && e->notation()) {
                n = e->notation();
            }
        }
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        if (!sc) {
            autotestLog("  part score not found");
        } else {
            auto rests = [&]() {
                std::map<QString, QString> out;
                for (const engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
                    int k = 0;
                    for (const engraving::Segment* seg = m->first(engraving::SegmentType::ChordRest); seg;
                         seg = seg->next(engraving::SegmentType::ChordRest)) {
                        const engraving::EngravingItem* e = seg->element(0);
                        if (e && e->isRest() && engraving::toRest(e)->beam()) {
                            const engraving::Beam* b = engraving::toRest(e)->beam();
                            out[QString("bar %1 rest %2").arg(m->no() + 1).arg(++k)]
                                = QString("y %1, beam y %2").arg(e->y() / sc->style().spatium(), 0, 'f', 2)
                                  .arg(b->ldata() ? b->pagePos().y() - e->pagePos().y() + e->y() : 0.0, 0, 'f', 1);
                        }
                    }
                }
                return out;
            };
            sc->setLayoutAll();
            sc->doLayout();
            const auto before = rests();
            engraving::Articulation* art = nullptr;
            engraving::Measure* target = sc->firstMeasure();
            for (int i = 1; i < bar && target; ++i) {
                target = target->nextMeasure();
            }
            for (engraving::Segment* seg = target ? target->first(engraving::SegmentType::ChordRest) : nullptr; seg && !art;
                 seg = seg->next(engraving::SegmentType::ChordRest)) {
                engraving::EngravingItem* e = seg->element(0);
                if (e && e->isChord() && !engraving::toChord(e)->articulations().empty()) {
                    art = engraving::toChord(e)->articulations().front();
                }
            }
            if (!art) {
                autotestLog("  no articulation in that bar");
            } else {
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Move"));
                art->undoChangeProperty(engraving::Pid::OFFSET, art->offset() + engraving::PointF(0.0, 0.5 * sc->style().spatium()));
                n->undoStack()->commitChanges();
                n->notationChanged().notify();
                QCoreApplication::processEvents();
                const auto after = rests();
                int changed = 0;
                for (const auto& [k, v] : after) {
                    auto it = before.find(k);
                    if (it == before.end() || it->second != v) {
                        autotestLog("  " + k + ": " + (it == before.end() ? QString("-") : it->second) + "  ->  " + v);
                        ++changed;
                    }
                }
                autotestLog(QString("  %1 of %2 beamed rests placed differently after the edit").arg(changed).arg(after.size()));
            }
        }
    } else if (step.startsWith("formatdiff:")) {
        // formatdiff:<part score A>|<part score B>: how B's formatting differs from A's: system and page breaks,
        // bar widths, texts (offset, alignment, font, placement, visibility; matched by place and words), frames
        const QString an = step.mid(11).section('|', 0, 0), bn = step.mid(11).section('|', 1);
        engraving::Score* A = nullptr;
        engraving::Score* B = nullptr;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == an && e->notation()) {
                A = e->notation()->elements()->msScore();
            }
            if (e->name() == bn && e->notation()) {
                B = e->notation()->elements()->msScore();
            }
        }
        if (!A || !B) {
            autotestLog("  part score not found");
        } else {
            A->doLayout();
            B->doLayout();
            for (engraving::Score* sc : { A, B }) {
                QStringList ls;
                for (const engraving::SystemLock* l : sc->systemLocks()->allLocks()) {
                    ls << QString("%1-%2").arg(l->startMB()->isMeasure() ? engraving::toMeasure(l->startMB())->no() + 1 : -1)
                          .arg(l->endMB()->isMeasure() ? engraving::toMeasure(l->endMB())->no() + 1 : -1);
                }
                autotestLog(QString("    locks %1: %2").arg(sc == A ? "A" : "B", ls.join(" ")));
            }
            int breaks = 0, widths = 0, texts = 0, textsMissing = 0, frames = 0, systemsA = 0, systemsB = 0;
            for (engraving::Measure* m = A->firstMeasure(); m; m = m->nextMeasure()) {
                engraving::Measure* o = B->tick2measure(m->tick());
                if (!o) {
                    continue;
                }
                if (m->lineBreak() != o->lineBreak() || m->pageBreak() != o->pageBreak()) {
                    autotestLog(QString("    bar %1: A line %2 page %3 locked %4; B line %5 page %6 locked %7").arg(m->no() + 1)
                                .arg(m->lineBreak()).arg(m->pageBreak()).arg(A->systemLocks()->lockContaining(m) != nullptr)
                                .arg(o->lineBreak()).arg(o->pageBreak()).arg(B->systemLocks()->lockContaining(o) != nullptr));
                }
                breaks += (m->lineBreak() != o->lineBreak()) + (m->pageBreak() != o->pageBreak());
                widths += m->userStretch() != o->userStretch();
            }
            for (engraving::Page* pg : A->pages()) {
                systemsA += int(pg->systems().size());
            }
            for (engraving::Page* pg : B->pages()) {
                systemsB += int(pg->systems().size());
            }
            std::multimap<std::pair<int, QString>, engraving::EngravingItem*> bt;
            for (engraving::Segment* seg = B->firstSegment(engraving::SegmentType::ChordRest); seg; seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    if (e->isTextBase()) {
                        bt.emplace(std::make_pair(seg->tick().ticks(), e->isHarmony() ? QString("#h%1").arg(int(engraving::toTextBase(e)->textStyleType())) : engraving::toTextBase(e)->plainText().toQString()), e);
                    }
                }
            }
            QStringList examples;
            for (engraving::Segment* seg = A->firstSegment(engraving::SegmentType::ChordRest); seg; seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    if (!e->isTextBase()) {
                        continue;
                    }
                    auto r = bt.equal_range(std::make_pair(seg->tick().ticks(), e->isHarmony() ? QString("#h%1").arg(int(engraving::toTextBase(e)->textStyleType())) : engraving::toTextBase(e)->plainText().toQString()));
                    if (r.first == r.second) {
                        ++textsMissing;
                        continue;
                    }
                    engraving::EngravingItem* o = nullptr;
                    for (auto it = r.first; it != r.second; ++it) {
                        if (it->second->type() == e->type() && it->second->getProperty(engraving::Pid::OFFSET) == e->getProperty(engraving::Pid::OFFSET)) {
                            o = it->second;
                        }
                    }
                    if (!o) {
                        for (auto it = r.first; it != r.second; ++it) {
                            o = it->second->type() == e->type() && !o ? it->second : o;
                        }
                    }
                    if (!o) {
                        ++textsMissing;
                        continue;
                    }
                    for (engraving::Pid pid : e->isHarmony() ? std::vector<engraving::Pid> { engraving::Pid::OFFSET, engraving::Pid::PLACEMENT, engraving::Pid::VISIBLE }
                                              : std::vector<engraving::Pid> { engraving::Pid::OFFSET, engraving::Pid::ALIGN, engraving::Pid::PLACEMENT,
                                                engraving::Pid::VISIBLE, engraving::Pid::FONT_SIZE, engraving::Pid::FONT_FACE }) {
                        if (e->getProperty(pid) != o->getProperty(pid)) {
                            ++texts;
                            if (examples.size() < 6) {
                                examples << QString("%1 %2 \"%3\" %4 vs %5").arg(engraving::propertyName(pid)).arg(e->typeName()).arg(engraving::toTextBase(e)->plainText().toQString().left(20)).arg(e->getProperty(pid).value<engraving::PointF>().y()).arg(o->getProperty(pid).value<engraving::PointF>().y()) + QString(" cand %1 tick %2 track %3/%4 autoplace %5/%6").arg(std::distance(r.first, r.second)).arg(seg->tick().ticks()).arg(e->track()).arg(o->track()).arg(e->autoplace()).arg(o->autoplace());
                            }
                        }
                    }
                }
            }
            std::vector<engraving::Box*> ba, bb;
            for (engraving::MeasureBase* mb = A->first(); mb; mb = mb->next()) {
                if (mb->isVBox()) {
                    ba.push_back(engraving::toBox(mb));
                }
            }
            for (engraving::MeasureBase* mb = B->first(); mb; mb = mb->next()) {
                if (mb->isVBox()) {
                    bb.push_back(engraving::toBox(mb));
                }
            }
            for (size_t i = 0; i < ba.size() && i < bb.size(); ++i) {
                frames += ba[i]->getProperty(engraving::Pid::BOX_HEIGHT) != bb[i]->getProperty(engraving::Pid::BOX_HEIGHT);
            }
            autotestLog(QString("  %1 vs %2: systems %3/%4, break differences %5, bar widths %6, text properties %7 (%8), "
                                "texts missing %9, frames %10")
                        .arg(an, bn).arg(systemsA).arg(systemsB).arg(breaks).arg(widths).arg(texts).arg(examples.join("; "))
                        .arg(textsMissing).arg(frames));
        }
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
    } else if (step == "hidesettings") {
        // hidesettings: each part's lower staves' "Hide when empty" in the full score and in each part score (0 auto,
        // 1 always, 2 never)
        for (const engraving::Part* p : ms->parts()) {
            for (size_t i = 1; i < p->nstaves(); ++i) {
                const engraving::Staff* st = p->staves().at(i);
                QStringList where { QString("main %1").arg(int(st->hideWhenEmpty())) };
                for (const engraving::Staff* l : st->staffList()) {
                    if (l->score() != ms) {
                        where << QString("%1 %2").arg(l->score()->excerpt() ? l->score()->excerpt()->name().toQString() : QString("?"))
                            .arg(int(l->hideWhenEmpty()));
                    }
                }
                autotestLog(QString("  %1 staff %2: %3").arg(p->partName().toQString()).arg(i + 1).arg(where.join(", ")));
            }
        }
    } else if (step.startsWith("breakmmr:")) {
        // breakmmr:<system text>|<0 or 1>: whether that system text breaks multimeasure rests (as the Properties
        // panel's box sets it), then every system text that does
        const QString text = step.mid(9).section('|', 0, 0);
        const bool on = step.mid(9).section('|', 1) == "1";
        int set = 0;
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Breaks multimeasure rests"));
        for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
             seg = seg->next1(engraving::SegmentType::ChordRest)) {
            for (engraving::EngravingItem* e : seg->annotations()) {
                if (e->isSystemText() && toTextBase(e)->plainText().toQString().trimmed() == text) {
                    e->undoChangeProperty(engraving::Pid::BREAK_MMR, on);
                    ++set;
                }
            }
        }
        master->notation()->undoStack()->commitChanges();
        QStringList breaking;
        for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
             seg = seg->next1(engraving::SegmentType::ChordRest)) {
            for (engraving::EngravingItem* e : seg->annotations()) {
                if (e->isSystemText() && e->getProperty(engraving::Pid::BREAK_MMR).toBool()) {
                    breaking << toTextBase(e)->plainText().toQString().trimmed();
                }
            }
        }
        autotestLog(QString("  set %1; breaking: %2").arg(set).arg(breaking.join(", ")));
    } else if (step.startsWith("resetpart:")) {
        // resetpart:<part score name>: the tab menu's Reset part score… after "Reset" in its question
        int done = 0;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == step.mid(10)) {
                resetPartScoreNow(e);
                ++done;
            }
        }
        autotestLog(QString("  reset %1").arg(done));
    } else if (step.startsWith("dyncollide")) {
        // dyncollide[:<part score>]: dynamics whose box overlaps a note, stem, ledger line, accidental or dot of their
        // staff, in each part score (or that one), as laid out now
        const QString only = step.section(':', 1);
        int total = 0;
        int checked = 0;
        std::vector<std::pair<QString, engraving::Score*> > scores { { QString("main score"), ms } };
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->notation()) {
                scores.emplace_back(e->name(), e->notation()->elements()->msScore());
            }
        }
        for (const auto& [ename, es] : scores) {
            if (!only.isEmpty() && ename != only) {
                continue;
            }
            es->setLayoutAll();
            es->doLayout();
            for (engraving::Segment* seg = es->firstSegment(engraving::SegmentType::ChordRest); seg;
                 seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* a : seg->annotations()) {
                    if (!a->isDynamic() || !a->visible()) {
                        continue;
                    }
                    ++checked;
                    if (checked <= 3) {
                        autotestLog(QString("    check %1 bar %2 box %3,%4 %5x%6").arg(ename).arg(seg->measure()->no() + 1)
                                    .arg(a->pageBoundingRect().x()).arg(a->pageBoundingRect().y()).arg(a->pageBoundingRect().width()).arg(a->pageBoundingRect().height()));
                    }
                    const double near = 0.25 * es->style().spatium();
                    const RectF box = a->pageBoundingRect().adjusted(-near, -near, near, near);
                    QStringList hits;
                    // the chords of its staff in this bar and the next one
                    engraving::Measure* m = seg->measure();
                    for (engraving::Measure* mm = m; mm && mm != (m->nextMeasure() ? m->nextMeasure()->nextMeasure() : nullptr);
                         mm = mm->nextMeasure()) {
                        for (engraving::Segment* s2 = mm->first(engraving::SegmentType::ChordRest); s2;
                             s2 = s2->next(engraving::SegmentType::ChordRest)) {
                            for (engraving::track_idx_t t = a->staffIdx() * engraving::VOICES; t < (a->staffIdx() + 1) * engraving::VOICES; ++t) {
                                engraving::EngravingItem* cr = s2->element(t);
                                if (!cr || !cr->isChord()) {
                                    continue;
                                }
                                engraving::Chord* c = engraving::toChord(cr);
                                c->scanElements([&](engraving::EngravingItem* el) {
                                    if (!(el->isNote() || el->isStem() || el->isLedgerLine() || el->isAccidental() || el->isNoteDot())) {
                                        return;
                                    }
                                    if (el->pageBoundingRect().intersects(box)) {
                                        hits << QString("%1 bar %2").arg(el->typeName()).arg(mm->no() + 1);
                                    }
                                });
                            }
                        }
                        if (!mm->nextMeasure()) {
                            break;
                        }
                    }
                    if (!hits.isEmpty()) {
                        hits.removeDuplicates();
                        ++total;
                        autotestLog(QString("  %1: %2 at bar %3, offset %4,%5, autoplace %6: %7").arg(ename,
                                    engraving::toDynamic(a)->plainText().toQString()).arg(seg->measure()->no() + 1)
                                    .arg(a->offset().x() / es->style().spatium()).arg(a->offset().y() / es->style().spatium())
                                    .arg(a->autoplace() ? "on" : "off").arg(hits.join(", ")));
                    }
                }
            }
        }
        autotestLog(QString("  %1 dynamic(s) on notes, of %2").arg(total).arg(checked));
    } else if (step.startsWith("respace:")) {
        // respace:<part score name>: the tab menu's Respace lyrics (without its message)
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->name() == step.mid(8) && e->notation()) {
                master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Respace lyrics"));
                const int n = respaceLyricsIn(e->notation()->elements()->msScore());
                master->notation()->undoStack()->commitChanges();
                autotestLog(QString("  %1 change(s)").arg(n));
            }
        }
    } else if (step == "voicerefresh") {
        // voicerefresh: the + Voice part scores remade where broken and formatted again, as at the start of an export
        autotestLog(QString("  formatted %1").arg(refreshVoiceScores()));
    } else if (step.startsWith("voiceadd:") || step == "voicescores") {
        // voiceadd:<part score name>: Add Voice to a part…; voicescores: the + Voice part scores, what each is made
        // from and its staves (the Voice's small or not), and the part scores that could get one
        if (step.startsWith("voiceadd:")) {
            const RetVal<QString> r = addVoiceToPartScore(step.mid(9));
            autotestLog("  " + (r.ret ? QString("made ") + r.val : QString("refused: ") + QString::fromStdString(r.ret.toString())));
        }
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents();
        }
        for (const VoiceScore& v : voiceScores(master)) {
            QStringList staves;
            INotationPtr n = v.score->notation();
            if (engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr) {
                for (const engraving::Part* p : es->parts()) {
                    staves << p->partName().toQString() + (p->staves().front()->staffType(engraving::Fraction(0, 1))->isSmall() ? " (small)" : "");
                }
            }
            autotestLog(QString("  %1 <- %2: %3").arg(v.score->name(), v.sourceName, staves.join(", ")));
        }
        autotestLog("  choices: " + voicePartScoreChoices().join(", "));
    } else if (step == "parts") {
        for (const engraving::Part* p : ms->parts()) {
            autotestLog(QString("  %1 (%2)").arg(p->partName().toQString(), p->instrumentId().toQString()));
        }
    } else if (step.startsWith("setstatus:")) {
        // setstatus:<part name>|<status number, -1 = no tag>: tags every part with that name, then logs the stored keys
        const QString name = step.mid(10).section('|', 0, 0);
        const int st = step.section('|', 1, 1).toInt();
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == name) {
                setPartStatus(p->id().toQString(), st);
            }
        }
        const Data d = load();
        for (const auto& [pid, key] : d.partStatus) {
            for (const engraving::Part* p : ms->parts()) {
                if (p->id().toQString() == pid) {
                    autotestLog(QString("  tag %1: %2").arg(p->partName().toQString(), key));
                }
            }
        }
        for (const StarScoreTodoItem& t : todoList()) {
            autotestLog(QString("  todo %1: %2").arg(t.title).arg(t.status));
        }
    } else if (step == "status") {
        // each arrangement's status (0 Empty … 5 Finished) and each section's
        const Data d = load();
        for (const StarScoreSection& s : d.sections) {
            autotestLog(QString("  section %1: %2 (skips %3)").arg(s.name).arg(int(s.status)).arg(s.autoSkipSheets.join(",")));
        }
        for (const StarScoreArrangement& a : d.arrangements) {
            autotestLog(QString("  arrangement %1: %2").arg(a.name).arg(int(arrangementStatus(a.id))));
        }
    } else if (step.startsWith("sectionparts:")) {
        // sectionparts:<section name>: its parts (shown or not, stand-in or not) with their status tags
        const Data d = load();
        for (const StarScoreSection& s : d.sections) {
            if (s.name != step.mid(13)) {
                continue;
            }
            for (const QString& pid : s.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                auto it = d.partStatus.find(pid);
                autotestLog(QString("  %1 (%2, %3)%4%5: %6").arg(p ? p->partName().toQString() : pid, p ? p->instrumentId().toQString() : QString())
                            .arg(p && p->show() ? "visible" : "hidden").arg(s.shownPartIds.contains(pid) ? " shown" : "")
                            .arg(s.alternates.count(pid) ? " alternate" : "").arg(it == d.partStatus.end() ? QString("(no tag)") : it->second));
            }
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
    } else if (step.startsWith("auditfile:")) {
        // auditfile:<path>[:layout]: the Dashboard's audit of another song's file (a copy is loaded, as a library scan
        // does); ":layout" lays the copy out on loading, as it was before, for comparing the two
        QString path = step.mid(10);
        m_autotestAuditWithLayout = path.endsWith(":layout");
        if (m_autotestAuditWithLayout) {
            path.chop(7);
        }
        const StarScoreAuditFileSummary s = auditFile(path, true);
        m_autotestAuditWithLayout = false;
        autotestLog(QString("  %1 error '%2' issues %3 likely %4 arrangements %5 audited %6 changed %7 listen %8/%9")
                    .arg(path, s.error).arg(s.openIssues).arg(s.likelyErrors).arg(s.arrangements).arg(s.arrangementsAudited)
                    .arg(s.arrangementsChanged).arg(s.listenApproved).arg(s.listenSteps));
        for (const StarScoreFileArrangement& a : s.arrangementList) {
            autotestLog(QString("  arr %1 [%2]: status %3 audited %4 changed %5 issues %6 unfinished %7")
                        .arg(a.name, a.column).arg(a.status).arg(a.audited).arg(a.changedSinceAudit).arg(a.openIssues)
                        .arg(a.unfinished.join("; ")));
        }
        for (const auto& [key, st] : s.sectionStatus) {
            autotestLog(QString("  section %1: status %2").arg(key).arg(st));
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
    } else if (step.startsWith("staffgaps:")) {
        // staffgaps:<part score name or "main">: laid out, the first three systems' staves (name, y in spaces)
        const QString bn = step.mid(10);
        engraving::Score* sc = nullptr;
        if (bn == "main") {
            sc = ms;
        } else {
            for (const IExcerptNotationPtr& e : master->excerpts()) {
                if (e->notation() && (e->name() == bn || (!sc && bn.startsWith('~') && e->name().contains(bn.mid(1))))) {
                    sc = e->notation()->elements()->msScore();
                    autotestLog("  " + e->name());
                }
            }
        }
        if (!sc) {
            autotestLog("  part score not found");
        } else {
            sc->doLayout();
            int shown = 0;
            for (const engraving::System* sys : sc->systems()) {
                if (sys->staves().empty() || sys->vbox()) {
                    continue;
                }
                if (++shown > 3) {
                    break;
                }
                QStringList ys;
                for (size_t i = 0; i < sys->staves().size(); ++i) {
                    if (sys->staff(i)->show()) {
                        ys << QString("%1 %2 (reaches %3 below)").arg(sc->staff(i)->part()->partName().toQString())
                              .arg(sys->staff(i)->y() / sc->style().spatium(), 0, 'f', 2)
                              .arg(sys->staff(i)->skyline().south().max() / sc->style().spatium(), 0, 'f', 2);
                    }
                }
                autotestLog(QString("  system at %1: ").arg(sys->pagePos().y() / sc->style().spatium(), 0, 'f', 2) + ys.join(" | "));
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
    } else if (step.startsWith("instrument:")) {
        // instrument:<part name>|<instrument id>: the part put on another instrument, as the StarScore bar's Change an
        // instrument menu does; then its name and instrument
        const QStringList a = step.mid(11).split('|');
        QString pid;
        for (const engraving::Part* p : ms->parts()) {
            if (p->partName().toQString() == a.value(0)) {
                pid = idText(p);
            }
        }
        const Ret r = changePartInstrument(pid, a.value(1));
        const engraving::Part* p = masterScore() ? masterScore()->partById(ID(pid)) : nullptr;
        autotestLog(QString("  %1: %2 -> %3 (%4)").arg(a.value(0), r ? "ok" : QString::fromStdString(r.toString()),
                                                       p ? p->partName().toQString() : QString("?"),
                                                       p ? p->instrumentId().toQString() : QString("?")));
    } else if (step.startsWith("doubler:")) {
        // doubler:<horns>: what the doubler is recommended to play in a new <horns>-horn section of this song
        autotestLog("  " + songDoublerInstrumentId(step.mid(8).toInt()));
    } else if (step.startsWith("arrangement:")) {
        // arrangement:<arrangement key>[:<doubler id>[:<low horn id>]], as the StarScore bar's Add arrangement menu makes
        // it (with the Add arrangement dialog's choices)
        const QStringList a = step.mid(QString("arrangement:").size()).split(':');
        QStringList sectionsBefore;
        for (const StarScoreSection& sec : load().sections) {
            sectionsBefore << sec.id;
        }
        const RetVal<QString> made = createArrangementFromTemplate(a.value(0), a.value(1), a.value(2));
        autotestLog(QString("  %1: %2").arg(a.value(0), made.ret ? made.val : QString::fromStdString(made.ret.toString())));
        // the 4-Horn 2-Trumpet's parts copied from the 5-Horn and the 3-Horn Standard, and the 1-Horn's Trumpet from
        // the 2-Horn's, as after the menu (no question)
        for (const StarScoreSection& sec : load().sections) {
            const bool twoTrumpet = sec.templateKey == starscore::TWO_TRUMPET_4H;
            if (made.ret && (twoTrumpet || (sec.templateKey == "1-horn" && !sectionsBefore.contains(sec.id)))) {
                const auto pairs = twoTrumpet ? twoTrumpetSources(sec) : oneHornSources(sec);
                for (const auto& [from, to] : pairs) {
                    autotestLog(QString("  copy %1 -> %2").arg(partScoreName(from), partScoreName(to)));
                }
                autotestLog(QString("  copied %1").arg(copyHornParts(pairs)));
            }
        }
    } else if (step.startsWith("filenew:") || step.startsWith("movesong:") || step == "songplace") {
        // filenew:<kind>: the open new score filed as File › New does (STARSCORE_AUTOTEST_PROJECTS is Projects and
        // Sheets); movesong:<1 Starsign song folder>: Move to…; songplace: the song's kind, folder and file
        const QString projects = qEnvironmentVariable("STARSCORE_AUTOTEST_PROJECTS");
        if (!projects.isEmpty()) {
            setProjectsFolder(projects);
        }
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (!band.isEmpty()) {
            setBandFolder(band);
        }
        if (step.startsWith("filenew:")) {
            const RetVal<QString> r = fileNewSong(step.mid(8));
            for (int i = 0; i < 20; ++i) {
                QCoreApplication::processEvents();
            }
            autotestLog("  " + (r.ret ? QDir(projects).relativeFilePath(r.val) : QString::fromStdString(r.ret.toString())));
        } else if (step.startsWith("movesong:")) {
            const RetVal<QString> r = moveSongTo(step.mid(9));
            autotestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.toString())));
        }
        autotestLog(QString("  kind %1, folder %2, file %3").arg(songKind(), songGroup(),
                                                               QDir(projects).relativeFilePath(mainProjectPath().toQString())));
    } else if (step.startsWith("new:")) {
        // new:<arrangement key>[:<doubler id>[:<low horn id>]]: a new StarScore as File › New makes it (title "Autotest"),
        // which replaces the open song; its parts and sections are logged
        const QStringList a = step.mid(QString("new:").size()).split(':');
        StarScoreNewOptions o;
        o.title = "Autotest";
        o.arrangementTemplateKey = a.value(0);
        o.doublerInstrumentId = a.value(1);
        o.lowHornInstrumentId = a.value(2);
        if (a.value(3).contains('/')) {   // :<time signature> (6/8), then :<tempo>
            o.timeSigNumerator = a.value(3).section('/', 0, 0).toInt();
            o.timeSigDenominator = a.value(3).section('/', 1).toInt();
        }
        if (a.value(4).toInt() > 0) {
            o.tempoBpm = a.value(4).toInt();
        }
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
        if (engraving::MasterScore* nms = masterScore()) {
            for (engraving::Segment* seg = nms->firstSegment(engraving::SegmentType::ChordRest); seg; seg = nullptr) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    if (e && e->isTempoText()) {
                        autotestLog(QString("  tempo marking: %1 (%2 quarters a minute)").arg(toTempoText(e)->xmlText().toQString())
                                    .arg(toTempoText(e)->tempo().val * 60.0));
                        break;
                    }
                }
            }
        }
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
        if (IMasterNotationPtr nm = globalContext()->currentMasterNotation()) {
            QStringList open, closed;
            for (const IExcerptNotationPtr& e : nm->excerpts()) {
                (e && e->notation() && e->notation()->isOpen() ? open : closed) << (e ? e->name() : QString());
            }
            autotestLog(QString("  open tabs: %1").arg(open.join(", ")));
            autotestLog(QString("  closed part scores: %1").arg(closed.join(", ")));
        }
    } else if (step.startsWith("addlock:")) {
        // addlock:<part score>:<first bar>-<last bar>: locks those bars into one system there, as Format › Lock does
        const QString who = step.mid(step.indexOf(':') + 1, step.lastIndexOf(':') - step.indexOf(':') - 1);
        const QString range = step.mid(step.lastIndexOf(':') + 1);
        const int from = range.section('-', 0, 0).toInt();
        const int to = range.section('-', 1, 1).toInt();
        IMasterNotationPtr nm = globalContext()->currentMasterNotation();
        INotationPtr n = nm && who == "(full score)" ? nm->notation() : nullptr;
        for (const IExcerptNotationPtr& e : nm ? nm->excerpts() : ExcerptNotationList()) {
            if (e && e->name() == who) {
                n = e->notation();
            }
        }
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        engraving::Measure* a = sc ? sc->firstMeasure() : nullptr;
        for (int i = 1; a && i < from; ++i) {
            a = a->nextMeasure();
        }
        engraving::Measure* b = a;
        for (int i = from; b && i < to; ++i) {
            b = b->nextMeasure();
        }
        if (a && b) {
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Lock"));
            engraving::EditSystemLocks::undoAddSystemLock(sc, new engraving::SystemLock(a->coveringMMRestOrThis(), b->coveringMMRestOrThis()));
            n->undoStack()->commitChanges();
            n->notationChanged().notify();
        }
        QStringList locks;
        if (sc) {
            for (const engraving::SystemLock* l : sc->systemLocks()->allLocks()) {
                locks << QString("%1-%2").arg(l->startMB()->tick().ticks()).arg(l->endMB()->tick().ticks());
            }
        }
        autotestLog(QString("  locked bars %1-%2 in %3: %4; locks (ticks) %5").arg(from).arg(to).arg(who, a && b ? "done" : "no such bars",
                                                                                                    locks.join(" ")));
    } else if (step == "sectionbreaks" || step.startsWith("addsectionbreak:") || step.startsWith("removesectionbreak:")) {
        // sectionbreaks: the bars with a section break in the full score and in each part score (bar number, and "!"
        // where the bar's flag and its break element disagree); addsectionbreak:<part score>:<bar>
        // (removesectionbreak:…) adds (takes away) one there as Format › Add/Remove section break does, then lists them
        IMasterNotationPtr nm = globalContext()->currentMasterNotation();
        if (!nm) {
            autotestLog("  no score");
        } else {
            std::vector<std::pair<QString, INotationPtr> > all { { "(full score)", nm->notation() } };
            for (const IExcerptNotationPtr& e : nm->excerpts()) {
                if (e && e->notation()) {
                    all.emplace_back(e->name(), e->notation());
                }
            }
            if (!step.startsWith("sectionbreaks")) {
                // (part score names have colons: "2H: Trumpet")
                const QString who = step.mid(step.indexOf(':') + 1, step.lastIndexOf(':') - step.indexOf(':') - 1);
                const int bar = step.mid(step.lastIndexOf(':') + 1).toInt();
                const bool add = step.startsWith("add");
                for (const auto& [name, n] : all) {
                    if (name != who) {
                        continue;
                    }
                    engraving::Score* sc = n->elements()->msScore();
                    engraving::Measure* m = sc->firstMeasure();
                    for (int i = 1; m && i < bar; ++i) {
                        m = m->nextMeasure();
                    }
                    if (m) {
                        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Section break"));
                        m->undoSetBreak(add, engraving::LayoutBreakType::SECTION);
                        n->undoStack()->commitChanges();
                        n->notationChanged().notify();
                    }
                    autotestLog(QString("  %1 section break at bar %2 of %3: %4").arg(add ? "added" : "took away").arg(bar).arg(who,
                                                                                                                    m ? "done" : "no such bar"));
                }
            }
            std::map<QString, QStringList> byBars;
            for (const auto& [name, n] : all) {
                QStringList bars;
                int no = 1;
                for (engraving::Measure* m = n->elements()->msScore()->firstMeasure(); m; m = m->nextMeasure(), ++no) {
                    const bool element = m->sectionBreakElement() != nullptr;
                    if (m->sectionBreak() || element) {
                        bars << QString::number(no) + (m->sectionBreak() != element ? "!" : "");
                    }
                }
                byBars[bars.join(" ")] << name;
            }
            for (const auto& [bars, names] : byBars) {
                autotestLog(QString("  section breaks at bars [%1]: %2").arg(bars, names.join(", ")));
            }
        }
    } else if (step.startsWith("chordsymbols:")) {
        // chordsymbols:<staff>:<chord>|<chord>|…: types each chord symbol on bar 1, 2, … of that staff (counted from 1)
        // as the chord symbol tool does (linked into the part scores), then logs, in the full score and in each part
        // score showing that staff, the text the chord holds and the text it shows
        // (…@<staff>: then copies those bars of the staff and pastes them onto that staff, as copy and paste do)
        const int staff = step.section(':', 1, 1).toInt() - 1;
        const QString rest = step.section(':', 2);
        const QStringList chords = rest.section('@', 0, 0).split('|');
        const int pasteTo = rest.contains('@') ? rest.section('@', 1).toInt() - 1 : -1;
        engraving::Measure* m = ms->firstMeasure();
        std::vector<engraving::Harmony*> made;
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Chord symbols"));
        for (const QString& text : chords) {
            if (!m || staff < 0 || staff >= int(ms->nstaves())) {
                break;
            }
            engraving::Segment* seg = m->first(engraving::SegmentType::ChordRest);
            for (engraving::EngravingItem* a : seg->annotations()) {
                if (a->isHarmony() && a->staffIdx() == size_t(staff)) {
                    ms->undoRemoveElement(a);
                    break;
                }
            }
            engraving::Harmony* h = engraving::Factory::createHarmony(seg);
            h->setTrack(staff * engraving::VOICES);
            h->setParent(seg);
            h->setHarmony(String::fromQString(text));
            h->setPlainText(h->harmonyName());
            ms->undoAddElement(h);
            made.push_back(h);
            m = m->nextMeasure();
        }
        master->notation()->undoStack()->commitChanges();
        ms->setLayoutAll();
        ms->doLayout();
        if (pasteTo >= 0 && pasteTo < int(ms->nstaves()) && m) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Paste chord symbols"));
            ms->deselectAll();
            ms->selection().setRangeTicks(engraving::Fraction(0, 1), m->tick(), staff, staff + 1);
            ms->selection().updateSelectedElements();
            const ByteArray mime = ms->selection().mimeData();
            ms->deselectAll();
            engraving::XmlReader reader(mime);
            const bool ok = !mime.empty() && ms->pasteStaff(reader, ms->firstMeasure()->first(engraving::SegmentType::ChordRest), pasteTo);
            master->notation()->undoStack()->commitChanges();
            autotestLog(QString("  pasted onto staff %1: %2").arg(pasteTo + 1).arg(ok ? "ok" : "FAILED"));
            made.clear();
            for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg && seg->tick() < m->tick();
                 seg = seg->next1(engraving::SegmentType::ChordRest)) {
                for (engraving::EngravingItem* a : seg->annotations()) {
                    if (a->isHarmony() && a->staffIdx() == size_t(pasteTo)) {
                        made.push_back(engraving::toHarmony(a));
                    }
                }
            }
            ms->setLayoutAll();
            ms->doLayout();
        }
        std::vector<engraving::Score*> scores { ms };
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (e->notation()) {
                scores.push_back(e->notation()->elements()->msScore());
            }
        }
        for (engraving::Score* sc : scores) {
            sc->setLayoutAll();
            sc->doLayout();
            QStringList seen;
            for (engraving::Harmony* h : made) {
                const engraving::Harmony* here = sc->isMaster() ? h : nullptr;
                if (!here && h->links()) {
                    for (engraving::EngravingObject* o : *h->links()) {
                        if (o->score() == sc) {
                            here = engraving::toHarmony(o);
                        }
                    }
                }
                if (here) {
                    seen << QString("%1 -> \"%2\"").arg(here->harmonyName().toQString(), here->displayText().toQString());
                }
            }
            if (!seen.isEmpty()) {
                autotestLog(QString("  [%1] %2").arg(sc->isMaster() ? QString("full score") : sc->excerpt()->name().toQString(),
                                                     seen.join("  ")));
            }
        }
    } else if (step == "tabs") {
        // the part scores open as tabs, and the ones closed
        if (IMasterNotationPtr nm = globalContext()->currentMasterNotation()) {
            QStringList open, closed;
            for (const IExcerptNotationPtr& e : nm->excerpts()) {
                (e && e->notation() && e->notation()->isOpen() ? open : closed) << (e ? e->name() : QString());
            }
            autotestLog(QString("  open tabs: %1").arg(open.join(", ")));
            autotestLog(QString("  closed part scores: %1").arg(closed.join(", ")));
        }
    } else if (step == "styles") {
        autotestLog(QString("  restyled %1").arg(applyStyles()));
    } else if (step.startsWith("version:")) {
        // version:1.2.3: the version number set in the song and every part score's footer (as an export does first)
        setScoreVersion(step.mid(8));
        autotestLog(QString("  version %1").arg(scoreVersion()));
    } else if (step == "export" || step.startsWith("export:")) {
        // export:<text>[|<text>…]: only the sheets whose path holds one of the texts (export:Section Score), to time or
        // check a few
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (band.isEmpty() || !QDir(band).exists()) {
            autotestLog("  STARSCORE_AUTOTEST_BAND missing");
        } else {
            setBandFolder(band);
            QStringList only;
            if (step.startsWith("export:")) {
                const QString text = step.mid(7);
                const QStringList texts = text.split('|', Qt::SkipEmptyParts);
                const RetVal<StarScoreBandExportPlan> plan = planBandExport();
                for (const StarScoreBandFile& f : plan.ret ? plan.val.files : std::vector<StarScoreBandFile>()) {
                    if (std::any_of(texts.begin(), texts.end(), [&](const QString& t) { return f.relativePath.contains(t); })) {
                        only << f.relativePath;
                    }
                }
                autotestLog(QString("  %1 sheet(s) match \"%2\"").arg(only.size()).arg(text));
            }
            const RetVal<QString> r = exportToBandFolder(only);
            QString text = r.ret ? r.val : QString::fromStdString(r.ret.toString());
            text.replace("\n", "\n  ");
            autotestLog("  " + text);
        }
    } else if (step == "organize" || step == "organize:after") {
        // organize: "Run Folder Organization" on STARSCORE_AUTOTEST_BAND (organize:after: as after the export just
        // made), offline, the PDFs written as their HTML (no WebKit here); logs the run's summary
        const QString band = qEnvironmentVariable("STARSCORE_AUTOTEST_BAND");
        if (band.isEmpty() || !QDir(band).exists()) {
            autotestLog("  STARSCORE_AUTOTEST_BAND missing");
        } else {
            namespace org = starscore::org;
            org::RunRequest request;
            request.online = false;
            if (step == "organize:after") {
                request.scope = org::RunRequest::AfterExport;
                request.exported = takeLastExport();
            }
            org::Prompts prompts;
            prompts.askShows = [](const std::vector<org::NewShow>&, std::function<void(std::vector<org::ShowAnswer>)> answer) { answer({}); };
            prompts.confirmSongs = [](std::vector<org::ShowVideo>&, const QStringList&, std::function<void()> next) { next(); };
            org::Fetcher fetch = [](const QString&, bool, std::function<void(const QString&, int)> done) { done(QString(), 0); };
            // (STARSCORE_AUTOTEST_PROJECTS: a Projects and Sheets folder for the run too)
            std::shared_ptr<org::Organizer> organizer = org::Organizer::create(org::Paths::make(band, qEnvironmentVariable("STARSCORE_AUTOTEST_PROJECTS")), fetch, prompts);
            organizer->setWriteHtmlInstead(autotestDir() + "/org-html");
            QEventLoop loop;
            org::RunSummary result;
            bool finished = false;
            organizer->run(request, org::Progress {}, [&](org::RunSummary s) {
                result = s;
                finished = true;
                loop.quit();
            });
            if (!finished) {
                loop.exec();
            }
            autotestLog("  " + result.headline);
            for (const QString& l : result.lines) {
                autotestLog("  - " + l);
            }
            for (const QString& w : result.warnings) {
                autotestLog("  ! " + w);
            }
            // as the Organize window does: the open song follows its moved file
            followMovedSong(result.projectMoves);
            autotestLog("  open song: " + mainProjectPath().toQString());
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
    } else if (step.startsWith("importstaves:")) {
        // importstaves:<file.mscz>|<from>:<to>;<from>:<to>… (0-based staff numbers): each staff's music in that file
        // copied over the open song's staff, every bar (the two must have the same bars). System markings the copy
        // brings along twice (rehearsal marks, system texts, tempo, voltas…) are removed again.
        const QString arg = step.mid(13);
        const QString file = arg.section('|', 0, 0);
        INotationProjectPtr src = projectCreator()->newProject(iocContext());
        engraving::MasterScore* ms = masterScore();
        if (!ms || !src->load(io::path_t(file)) || !src->masterNotation()) {
            autotestLog("  couldn't load " + file);
        } else {
            engraving::MasterScore* ss = src->masterNotation()->masterScore();
            int bars = 0, same = 0;
            for (engraving::Measure *a = ms->firstMeasure(), *b = ss->firstMeasure(); a || b;
                 a = a ? a->nextMeasure() : nullptr, b = b ? b->nextMeasure() : nullptr) {
                ++bars;
                same += (a && b && a->tick() == b->tick() && a->ticks() == b->ticks()) ? 1 : 0;
            }
            autotestLog(QString("  bars: %1, the same in both: %2").arg(bars).arg(same));
            if (same == bars) {
                const int spannersBefore = int(ms->spanner().size());
                globalContext()->currentMasterNotation()->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Import staves"));
                const bool mm = ms->style().styleB(engraving::Sid::createMultiMeasureRests);
                if (mm) {
                    ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, false);
                }
                ss->style().set(engraving::Sid::createMultiMeasureRests, false);
                ss->setLayoutAll();
                ss->doLayout();
                ms->setLayoutAll();
                ms->doLayout();
                // what is on the staves now, before the paste: system markings kept as they are
                std::map<QString, int> keep;
                for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
                     seg = seg->next1(engraving::SegmentType::ChordRest)) {
                    for (engraving::EngravingItem* e : seg->annotations()) {
                        if (e->systemFlag() || e->isRehearsalMark() || e->isTempoText()) {
                            keep[QString("%1|%2|%3").arg(int(e->type())).arg(seg->tick().ticks()).arg(e->track())]++;
                        }
                    }
                }
                std::set<engraving::Spanner*> spannersThen;
                for (const auto& [t, sp] : ms->spanner()) {
                    spannersThen.insert(sp);
                }
                for (const QString& pair : arg.section('|', 1).split(';', Qt::SkipEmptyParts)) {
                    const engraving::staff_idx_t from = pair.section(':', 0, 0).toUInt();
                    const engraving::staff_idx_t to = pair.section(':', 1, 1).toUInt();
                    ss->deselectAll();
                    ss->selection().setRangeTicks(engraving::Fraction(0, 1), ss->lastMeasure()->endTick(), from, from + 1);
                    ss->selection().updateSelectedElements();
                    const ByteArray mime = ss->selection().mimeData();
                    ss->deselectAll();
                    engraving::XmlReader reader(mime);
                    const bool ok = !mime.empty()
                                    && ms->pasteStaff(reader, ms->firstMeasure()->first(engraving::SegmentType::ChordRest), to);
                    ms->deselectAll();
                    autotestLog(QString("  staff %1 -> %2: %3").arg(from).arg(to).arg(ok ? "pasted" : "FAILED"));
                }
                // system markings and system lines the paste added a second time
                int removed = 0;
                std::map<QString, int> seen;
                std::vector<engraving::EngravingItem*> extra;
                for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg;
                     seg = seg->next1(engraving::SegmentType::ChordRest)) {
                    for (engraving::EngravingItem* e : seg->annotations()) {
                        if (e->systemFlag() || e->isRehearsalMark() || e->isTempoText()) {
                            const QString key = QString("%1|%2|%3").arg(int(e->type())).arg(seg->tick().ticks()).arg(e->track());
                            if (++seen[key] > std::max(1, keep[key])) {
                                extra.push_back(e);
                            }
                        }
                    }
                }
                for (engraving::EngravingItem* e : extra) {
                    ms->undoRemoveElement(e);
                    ++removed;
                }
                std::vector<engraving::Spanner*> newSystemLines;
                for (const auto& [t, sp] : ms->spanner()) {
                    if (!spannersThen.count(sp) && sp->systemFlag()) {
                        newSystemLines.push_back(sp);
                    }
                }
                for (engraving::Spanner* sp : newSystemLines) {
                    ms->undoRemoveElement(sp);
                    ++removed;
                }
                if (mm) {
                    ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, true);
                }
                globalContext()->currentMasterNotation()->notation()->undoStack()->commitChanges();
                ms->setLayoutAll();
                ms->doLayout();
                if (INotationProjectPtr project = globalContext()->currentProject()) {
                    project->markAsUnsaved();
                }
                autotestLog(QString("  removed %1 system marking(s)/line(s) brought twice; spanners %2 -> %3")
                            .arg(removed).arg(spannersBefore).arg(ms->spanner().size()));
            }
        }
    } else if (step == "todouble" || step == "todouble:keepnumbers") {
        // todouble: the open song rewritten at twice its note lengths (a song written in half time made standard);
        // todouble:keepnumbers: numbered like the original bars (what the Double-Time sheets are made from)
        const RetVal<QString> r = convertTimeOf(globalContext()->currentProject(), true, step.endsWith("keepnumbers"));
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
    } else if (step.startsWith("progressshow:")) {
        // progressshow:1 / progressshow:0: View › Progress colors › Show progress colors
        setProgressColorsShown(step.endsWith("1"));
        autotestLog(QString("  shown: %1").arg(progressColorsShown()));
    } else if (step.startsWith("progress:")) {
        // progress:CODE:S1-S2:B1-B2 marks staves S1..S2 (0-based, in the score on screen) of bars B1..B2 (1-based):
        // CODE g, o, r, or c (clear)
        const QStringList a = step.split(':');
        INotationPtr n = globalContext()->currentNotation();
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        if (a.size() == 4 && sc) {
            const int s1 = a[2].section('-', 0, 0).toInt(), s2 = a[2].section('-', 1, 1).toInt();
            const int b1 = a[3].section('-', 0, 0).toInt(), b2 = a[3].section('-', 1, 1).toInt();
            engraving::Measure* m1 = sc->crMeasure(b1 - 1);
            engraving::Measure* m2 = sc->crMeasure(b2 - 1);
            if (m1 && m2) {
                sc->selection().setRangeTicks(m1->tick(), m2->endTick(), engraving::staff_idx_t(s1), engraving::staff_idx_t(s2 + 1));
                sc->selection().updateSelectedElements();
                const char code = a[1] == "c" ? 0 : a[1].at(0).toLatin1();
                const QString problem = markProgress(code);
                autotestLog("  " + (problem.isEmpty() ? QString("marked") : problem));
            } else {
                autotestLog("  no such bars");
            }
        }
        const QString tag = masterScore()->metaTag(String(engraving::starscore::PROGRESS_TAG)).toQString();
        autotestLog(QString("  %1 marks: %2").arg(tag.isEmpty() ? 0 : tag.count(';') + 1).arg(tag.left(300)));
    } else if (step.startsWith("partmark:") || step.startsWith("annotate:") || step.startsWith("annotatemark:")
               || step.startsWith("annotationunmark:")) {
        // In the score on screen (a part score), bar B (1-based), staff 0:
        // partmark:B|text|<words>, partmark:B|dynamic|mf, partmark:B|breath, partmark:B|fingering|3, partmark:B|hairpin:
        //   a marking added as MuseScore adds one (in the full score and other part scores too)
        // annotate:B|<player>|<words>: Add player annotation… at the bar's first note or rest
        // annotatemark:B|<player>: every marking in the bar selected, Mark as player annotation…
        // annotationunmark:B: every marking in the bar selected, Unmark player annotation
        const QStringList a = step.section(':', 1).split('|');
        INotationPtr n = globalContext()->currentNotation();
        engraving::Score* sc = n ? n->elements()->msScore() : nullptr;
        engraving::Measure* m = sc ? sc->crMeasure(a.value(0).toInt() - 1) : nullptr;
        engraving::ChordRest* cr = m ? m->first(engraving::SegmentType::ChordRest)->cr(0) : nullptr;
        if (!cr) {
            autotestLog("  no such bar");
        } else if (step.startsWith("partmark:")) {
            const QString kind = a.value(1);
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Autotest marking"));
            engraving::EngravingItem* e = nullptr;
            if (kind == "text") {
                engraving::StaffText* st = engraving::Factory::createStaffText(cr->segment());
                st->setPlainText(String(a.value(2)));
                e = st;
            } else if (kind == "dynamic") {
                engraving::Dynamic* d = engraving::Factory::createDynamic(cr->segment());
                d->setDynamicType(String(a.value(2)));
                e = d;
            } else if (kind == "breath") {
                engraving::Segment* bs = m->undoGetSegment(engraving::SegmentType::Breath, cr->endTick());
                engraving::Breath* b = engraving::Factory::createBreath(bs);
                b->setSymId(engraving::SymId::breathMarkComma);
                b->setTrack(cr->track());
                b->setParent(bs);
                sc->undoAddElement(b);
            } else if (kind == "fingering" && cr->isChord()) {
                engraving::Note* note = engraving::toChord(cr)->upNote();
                engraving::Fingering* f = engraving::Factory::createFingering(note);
                f->setPlainText(String(a.value(2)));
                f->setTrack(cr->track());
                f->setParent(note);
                sc->undoAddElement(f);
            } else if (kind == "hairpin") {
                // as MuseScore adds one from the palette over the bar
                engraving::Hairpin* h = engraving::Factory::createHairpin(sc->dummy()->segment());
                h->setHairpinType(engraving::HairpinType::CRESC_HAIRPIN);
                engraving::Segment* endSeg = m->nextMeasure() ? m->nextMeasure()->first(engraving::SegmentType::ChordRest) : nullptr;
                sc->cmdAddSpanner(h, cr->staffIdx(), cr->segment(), endSeg);
            }
            if (e) {
                e->setTrack(cr->track());
                e->setParent(cr->segment());
                sc->undoAddElement(e);
            }
            n->undoStack()->commitChanges();
            autotestLog("  added " + kind);
        } else if (step.startsWith("annotate:")) {
            // (bar B in a multimeasure rest: the rest shown is selected, as a click on it would)
            if (m->mmRest() && sc->style().styleB(engraving::Sid::createMultiMeasureRests)) {
                engraving::Segment* ms0 = m->mmRest()->first(engraving::SegmentType::ChordRest);
                if (ms0 && ms0->cr(cr->track())) {
                    cr = ms0->cr(cr->track());
                    autotestLog("  (the multimeasure rest selected)");
                }
            }
            sc->select(cr, engraving::SelectType::SINGLE);
            const QString problem = addPlayerAnnotation(a.value(1), a.value(2));
            autotestLog("  " + (problem.isEmpty() ? QString("added") : problem));
        } else {
            std::vector<engraving::EngravingItem*> items;
            for (engraving::Segment* seg = m->first(); seg; seg = seg->next()) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    items.push_back(e);
                }
                for (engraving::EngravingItem* e : seg->elist()) {
                    if (e && e->isBreath()) {
                        items.push_back(e);
                    } else if (e && e->isChord()) {
                        for (engraving::Articulation* ar : engraving::toChord(e)->articulations()) {
                            items.push_back(ar);
                        }
                        for (engraving::Note* nt : engraving::toChord(e)->notes()) {
                            for (engraving::EngravingItem* x : nt->el()) {
                                items.push_back(x);
                            }
                        }
                    }
                }
            }
            for (const auto& [tick, sp] : sc->spanner()) {
                if (sp->tick() >= m->tick() && sp->tick() < m->endTick() && sp->isHairpin()) {
                    items.push_back(sp);
                }
            }
            // annotatemark:B|<player>|<type>, annotationunmark:B|<type>: only items of that type (StaffText, Dynamic…)
            const QString onlyType = step.startsWith("annotatemark:") ? a.value(2) : a.value(1);
            sc->deselectAll();
            for (engraving::EngravingItem* e : items) {
                if (onlyType.isEmpty() || onlyType == e->typeName()) {
                    sc->select(e, engraving::SelectType::ADD);
                }
            }
            const StarScoreAnnotationTarget t = annotationTarget();
            autotestLog(QString("  target: sheet '%1' count %2 inOther %3 owners [%4] default '%5' players [%6] problem '%7'")
                        .arg(t.sheet).arg(t.count).arg(t.inOtherScores).arg(t.owners.join(",")).arg(t.defaultPlayer)
                        .arg(t.players.join(",")).arg(t.problem));
            const QString problem = step.startsWith("annotatemark:") ? markPlayerAnnotations(a.value(1)) : unmarkPlayerAnnotations();
            autotestLog("  " + (problem.isEmpty() ? QString("done") : problem));
        }
    } else if (step == "annotations") {
        // the song's player annotations: the tag, and each part score's, with where its copies are
        const QString tag = masterScore()->metaTag(String(engraving::starscore::ANNOTATIONS_TAG)).toQString();
        autotestLog(QString("  tag: %1").arg(tag));
        for (engraving::Excerpt* ex : ms->excerpts()) {
            for (const starscore::AnnotationItem& ai : starscore::annotationItems(ex->excerptScore())) {
                const engraving::Measure* mm = ai.item->findMeasure();
                QString text = ai.item->isTextBase() ? engraving::toTextBase(ai.item)->plainText().toQString() : QString();
                int linked = 0;
                for (const engraving::EngravingObject* l : ai.item->linkList()) {
                    if (l != ai.item) {
                        ++linked;
                        engraving::Score* ls = const_cast<engraving::EngravingObject*>(l)->score();
                        const engraving::EngravingItem* li = static_cast<const engraving::EngravingItem*>(l);
                        autotestLog(QString("    (this track %1 tick %2; linked track %3 tick %4 same parent %5)").arg(ai.item->track())
                                    .arg(ai.item->tick().ticks()).arg(li->track()).arg(li->tick().ticks())
                                    .arg(li->explicitParent() == ai.item->explicitParent()));
                        autotestLog(QString("    linked: %1 in %2, parent %3").arg(l->typeName())
                                    .arg(ls == ms ? QString("full score") : ls && ls->excerpt() ? ls->excerpt()->name().toQString() : QString("?"))
                                    .arg(l->explicitParent() ? l->explicitParent()->typeName() : "none"));
                    }
                }
                autotestLog(QString("  %1: %2 '%3' bar %4 by %5, linked copies %6").arg(ex->name().toQString())
                            .arg(ai.item->typeName()).arg(text).arg(mm ? mm->no() + 1 : (ai.item->isSpanner() ? -2 : -1))
                            .arg(ai.player).arg(linked));
            }
        }
        // what the full score has (staff texts by words), to see annotations aren't in it
        QStringList texts;
        for (engraving::Segment* seg = ms->firstSegment(engraving::SegmentType::ChordRest); seg; seg = seg->next1(engraving::SegmentType::ChordRest)) {
            for (engraving::EngravingItem* e : seg->annotations()) {
                if (e->isStaffText()) {
                    texts << engraving::toTextBase(e)->plainText().toQString();
                }
            }
        }
        autotestLog(QString("  full score staff texts: %1").arg(texts.join(" | ")));
    } else if (step.startsWith("setcomposer:")) {
        // setcomposer:<text>: the main score's composer text changed, as editing it does (one undo step)
        engraving::Text* t = nullptr;
        for (engraving::MeasureBase* mb = ms->first(); mb && !mb->isMeasure() && !t; mb = mb->next()) {
            for (engraving::EngravingItem* e : mb->el()) {
                if (!t && e && e->isText() && engraving::toText(e)->textStyleType() == engraving::TextStyleType::COMPOSER) {
                    t = engraving::toText(e);
                }
            }
        }
        if (t) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Edit text"));
            t->undoChangeProperty(engraving::Pid::TEXT, String::fromQString(step.mid(12)));
            master->notation()->undoStack()->commitChanges();
            master->notation()->notationChanged().notify();
        }
        autotestLog(t ? "  changed" : "  no composer text");
    } else if (step == "credits") {
        // credits: the composer text of the main score and every part score
        std::vector<std::pair<QString, engraving::Score*> > all { { QString("(main score)"), ms } };
        for (engraving::Excerpt* ex : ms->excerpts()) {
            all.push_back({ ex->name().toQString(), ex->excerptScore() });
        }
        std::map<QString, QStringList> byText;
        for (auto& [name, sc] : all) {
            byText[starscore::creditTexts(sc).section(QChar(0x1F), 0, 0)] << name;
        }
        for (const auto& [text, names] : byText) {
            autotestLog(QString("  \"%1\": %2 score(s)%3").arg(text).arg(names.size())
                        .arg(names.contains("(main score)") ? QString(" incl. main") : QString()));
        }
    } else if (step == "undo") {
        if (INotationPtr n = globalContext()->currentNotation()) {
            n->undoStack()->undo(nullptr);
        }
        const QString tag = masterScore()->metaTag(String(engraving::starscore::PROGRESS_TAG)).toQString();
        autotestLog(QString("  after undo %1 marks").arg(tag.isEmpty() ? 0 : tag.count(';') + 1));
    } else if (step.startsWith("archiverepairtest:")) {
        // archiverepairtest:DIR: DIR's .starscore files saved 10 minutes apart into a history; the history file then
        // damaged (a byte flipped in the 2nd version's frame, or cut short: archiverepairtest:DIR:cut, or cut short
        // inside its index: archiverepairtest:DIR:cutindex); one more save packed; reports what is left and checks
        // every remaining version unpacks identical
        namespace aa = mu::project::starscore;
        const QString root = aa::autosaveArchiveRoot();
        const QString dir = step.section(':', 1, 1);
        const bool cut = step.section(':', 2, 2) == "cut";
        const bool cutIndex = step.section(':', 2, 2) == "cutindex";
        const QString work = autotestDir() + "/repair";
        QDir().mkpath(work);
        const QString song = work + "/RPAR - Repair.starscore";
        const QString hist = root + "/RPAR - Repair.starhistory";
        QFile::remove(hist);
        const QStringList files = QDir(dir).entryList({ "*.starscore" }, QDir::Files, QDir::Name);
        QDateTime t(QDate(2026, 10, 1), QTime(12, 0));
        std::map<QDateTime, QString> made;
        for (int i = 0; i + 1 < files.size(); ++i) {
            QFile::remove(song);
            QFile::copy(dir + "/" + files[i], song);
            t = t.addSecs(600);
            if (!aa::archiveBeforeSave(song, t, false).isEmpty()) {
                made[t] = dir + "/" + files[i];
            }
            aa::packIncoming(t);
        }
        // damage it
        {
            QFile f(hist);
            f.open(QIODevice::ReadWrite);
            QByteArray all = f.readAll();
            if (cutIndex) {
                all.truncate(all.indexOf("\"versions\"") + 40);   // inside the JSON index
            } else if (cut) {
                all.truncate(all.size() * 2 / 3);
            } else {
                all[all.size() / 3] = char(all[all.size() / 3] ^ 0x5a);
            }
            f.resize(0);
            f.seek(0);
            f.write(all);
        }
        QFile::remove(song);
        QFile::copy(dir + "/" + files.last(), song);
        t = t.addSecs(600);
        if (!aa::archiveBeforeSave(song, t, false).isEmpty()) {
            made[t] = dir + "/" + files.last();
        }
        const int packed = aa::packIncoming(t);
        const auto versions = aa::archivedVersions("RPAR - Repair");
        QStringList damaged = QDir(root).entryList({ "RPAR - Repair.starhistory.damaged-*" }, QDir::Files);
        int same = 0;
        for (const auto& v : versions) {
            const QString out = aa::extractVersion("RPAR - Repair", v.time, work + "/out.starscore");
            auto entries = [](const QString& path) {
                std::map<std::string, QByteArray> m;
                muse::ZipReader z{ muse::io::path_t(path) };
                for (const auto& fi : z.fileInfoList()) {
                    if (fi.isFile) {
                        const muse::ByteArray d = z.fileData(fi.filePath.toStdString());
                        m[fi.filePath.toStdString()] = QByteArray(reinterpret_cast<const char*>(d.constData()), qsizetype(d.size()));
                    }
                }
                return m;
            };
            same += !out.isEmpty() && made.count(v.time) && entries(out) == entries(made[v.time]);
        }
        autotestLog(QString("  %1 saves; after damage: packed %2, %3 versions left, %4 identical, waiting in Incoming %5, damaged copies kept %6")
                    .arg(made.size()).arg(packed).arg(versions.size()).arg(same)
                    .arg(QDir(root + "/Incoming").entryList({ "RPAR - *" }, QDir::Files).size()).arg(damaged.join(", ")));
        QFile::remove(hist);
        for (const QString& d : damaged) {
            QFile::remove(root + "/" + d);
        }
    } else if (step.startsWith("autoarchivetest")) {
        // autoarchivetest[:DIR]: the autosave archive. With DIR, its .starscore files (oldest first, by name) are
        // saved 10 minutes apart, then every version is unpacked and checked against its file, entry by entry.
        // Then 400 simulated days of a song (saves every 10 minutes, an hour an evening, 2 evenings a week, each a
        // small change), reporting what is kept and the history file's size against the plain copies'.
        namespace aa = mu::project::starscore;
        const QString root = aa::autosaveArchiveRoot();
        auto entriesOf = [](const QString& path) {
            std::map<std::string, QByteArray> m;
            muse::ZipReader z{ muse::io::path_t(path) };
            for (const auto& fi : z.fileInfoList()) {
                if (fi.isFile) {
                    const muse::ByteArray d = z.fileData(fi.filePath.toStdString());
                    m[fi.filePath.toStdString()] = QByteArray(reinterpret_cast<const char*>(d.constData()), qsizetype(d.size()));
                }
            }
            return m;
        };
        const QString dir = step.section(':', 1);
        if (!dir.isEmpty()) {
            const QString work = autotestDir() + "/real";
            QDir().mkpath(work);
            const QString song = work + "/REAL - Test.starscore";
            QFile::remove(root + "/REAL - Test.starhistory");
            QStringList files = QDir(dir).entryList({ "*.starscore" }, QDir::Files, QDir::Name);
            QDateTime t(QDate(2026, 10, 1), QTime(12, 0));
            qint64 plain = 0;
            std::vector<std::pair<QDateTime, QString> > made;
            for (const QString& f : files) {
                QFile::remove(song);
                QFile::copy(dir + "/" + f, song);
                plain += QFileInfo(song).size();
                t = t.addSecs(600);
                if (!aa::archiveBeforeSave(song, t, false).isEmpty()) {
                    made.emplace_back(t, dir + "/" + f);
                }
                QElapsedTimer pt;
                pt.start();
                aa::packIncoming(t);
                autotestLog(QString("  %1: packed in %2 ms, history %3 bytes").arg(f).arg(pt.elapsed())
                            .arg(QFileInfo(root + "/REAL - Test.starhistory").size()));
            }
            autotestLog(QString("  %1 files, %2 bytes as files, %3 bytes packed").arg(files.size()).arg(plain)
                        .arg(QFileInfo(root + "/REAL - Test.starhistory").size()));
            int ok = 0;
            for (const auto& [time, original] : made) {
                QElapsedTimer et;
                et.start();
                QString err;
                const QString out = aa::extractVersion("REAL - Test", time, work + "/out.starscore", &err);
                const bool same = !out.isEmpty() && entriesOf(out) == entriesOf(original);
                ok += same ? 1 : 0;
                autotestLog(QString("  %1 -> %2 in %3 ms %4").arg(time.toString("HHmm"), same ? "identical" : "DIFFERENT")
                            .arg(et.elapsed()).arg(err));
            }
            autotestLog(QString("  %1 of %2 versions identical").arg(ok).arg(made.size()));
            // (left for the dialog; removed at the start of the next run)
        }

        // the simulation: a song of about 3 MB of XML, a few lines changed at each save
        QFile::remove(root + "/SIMU - Simulated.starhistory");
        for (const QString& f : QDir(root + "/Incoming").entryList({ "SIMU - *" }, QDir::Files)) {
            QFile::remove(root + "/Incoming/" + f);
        }
        const QString work = autotestDir() + "/sim";
        QDir().mkpath(work);
        const QString song = work + "/SIMU - Simulated.starscore";
        QStringList lines;
        for (int i = 0; i < 20000; ++i) {
            lines << QString("<Note><pitch>%1</pitch><tpc>%2</tpc><eid>e%3</eid></Note>").arg(40 + i % 40).arg(i % 33).arg(i);
        }
        qint64 plain = 0;
        int made = 0;
        QDateTime t;
        int edit = 0;
        for (int day = 0; day < 400; ++day) {
            if (day % 7 >= 2) {
                continue;
            }
            const QDateTime start(QDate(2026, 1, 5).addDays(day), QTime(19, 0));
            for (int m = 0; m < 60; m += 10) {
                t = start.addSecs(m * 60);
                for (int k = 0; k < 20; ++k) {
                    const int at = (edit * 7919 + k * 104729) % lines.size();
                    lines[at] = QString("<Note><pitch>%1</pitch><eid>x%2</eid></Note>").arg(edit % 80).arg(edit);
                    ++edit;
                }
                {
                    QFile::remove(song);
                    muse::ZipWriter z{ muse::io::path_t(song) };
                    const QByteArray xml = lines.join("\n").toUtf8();
                    z.addFile("SIMU.mscx", muse::ByteArray(reinterpret_cast<const uint8_t*>(xml.constData()), size_t(xml.size())));
                    z.close();
                }
                if (!aa::archiveBeforeSave(song, t, false).isEmpty()) {
                    ++made;
                    plain += QFileInfo(song).size();
                    aa::packIncoming(t);
                }
            }
            if (day == 0 || day == 30 || day == 399) {
                const auto versions = aa::archivedVersions("SIMU - Simulated");
                QStringList times;
                for (const auto& v : versions) {
                    times << v.time.toString("MM-dd HHmm");
                }
                autotestLog(QString("  day %1: %2 kept of %3 made (%4 bytes as files), history %5 bytes: %6").arg(day)
                            .arg(versions.size()).arg(made).arg(plain).arg(aa::archivedBytes("SIMU - Simulated"))
                            .arg(times.join(" | ")));
            }
        }
        const auto versions = aa::archivedVersions("SIMU - Simulated");
        QElapsedTimer et;
        et.start();
        const QString out = aa::extractVersion("SIMU - Simulated", versions.back().time, work + "/oldest.starscore");
        autotestLog(QString("  oldest version unpacked in %1 ms: %2").arg(et.elapsed()).arg(out.isEmpty() ? "FAILED" : "ok"));
        et.restart();
        const QString out2 = aa::extractVersion("SIMU - Simulated", versions.front().time, work + "/newest.starscore");
        const bool same = !out2.isEmpty() && entriesOf(out2) == entriesOf(song) ? false : true;
        autotestLog(QString("  newest version unpacked in %1 ms: %2").arg(et.elapsed()).arg(out2.isEmpty() ? "FAILED" : "ok"));
        (void)same;
        QFile::remove(root + "/SIMU - Simulated.starhistory");
    } else if (step == "import") {
        // import: the open MuseScore file made into a .starscore as the import dialog suggests (sections and
        // arrangements as planned, standardized); "save" then writes it
        const StarScoreImportPlan plan = planImport();
        autotestLog("  plan: " + plan.summary);
        std::map<QString, QString> sectionByPart;
        for (const StarScoreImportPart& p : plan.parts) {
            sectionByPart[p.partId] = p.suggestedSection;
            autotestLog(QString("  part %1 (%2) -> %3").arg(p.name, p.instrumentId, p.suggestedSection.isEmpty() ? "-" : p.suggestedSection));
        }
        autotestLog("  arrangements: " + plan.suggestedArrangements.join(", "));
        const Ret r = applyImport(sectionByPart, plan.suggestedArrangements, true);
        autotestLog(QString("  import: %1").arg(r ? "ok" : QString::fromStdString(r.toString())));
    } else if (step.startsWith("practicebook")) {
        // practicebook[:<days>]: a plan over the songs with practice items in STARSCORE_AUTOTEST_PRACTICE (a folder of
        // .starscore files), written as a book with the exercises engraved by this StarScore, plus plan.json and
        // ireal.html, into the autotest folder
        const QString root = qEnvironmentVariable("STARSCORE_AUTOTEST_PRACTICE");
        const int nDays = step.contains(':') ? step.section(':', 1).toInt() : 1;
        std::map<QString, practice::Library> libs;
        practice::PlanRequest req;
        req.kind = nDays > 1 ? "plan" : "routine";
        req.name = nDays > 1 ? "Autotest plan" : "Autotest routine";
        int n = 0;
        for (const practice::SongEntry& e : practice::scanLibraries(root, autotestDir() + "/libraries.json")) {
            libs[e.code] = practice::readLibrary(e.path);
            practice::PlanSong ps;
            ps.code = e.code;
            ps.title = e.title;
            ps.set = 1 + n++ / 6;
            ps.needs = libs[e.code].needs;
            req.songs.push_back(ps);
            autotestLog(QString("  library %1: %2 items").arg(e.title).arg(libs[e.code].items.size()));
        }
        const QDate start = QDate::currentDate();
        for (int i = 0; i < nDays; ++i) {
            req.days.push_back({ start.addDays(i), 90 });
        }
        if (nDays > 1) {
            req.showDate = start.addDays(nDays - 1);
        }
        practice::Progress progress(autotestDir() + "/progress.json");
        practice::Plan plan = practice::makePlan(req, libs, progress);
        plan.id = "autotest";   // saved as a plan too, so "uri:musescore://starscore/practicerunner?planId=autotest" opens it
        practice::savePlan(plan);
        {
            QFile f(autotestDir() + "/plan.json");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(plan.toJson()).toJson());
            }
            QFile h(autotestDir() + "/ireal.html");
            if (h.open(QIODevice::WriteOnly)) {
                h.write(practice::irealHtml(plan, libs).toUtf8());
            }
        }
        int rendered = 0;
        int images = 0;
        const bool ok = practice::writePracticeBook(plan, libs, autotestDir() + "/book.pdf", [&](const practice::Item& item, int) {
            const QList<QImage> pages = renderPracticeExercise(item.xml, 160);
            ++rendered;
            images += int(pages.size());
            if (rendered <= 3 && !pages.isEmpty()) {
                pages.front().save(autotestDir() + QString("/exercise-%1.png").arg(rendered));
            }
            return pages;
        });
        autotestLog(QString("  book: %1, %2 exercises engraved, %3 images").arg(ok ? "written" : "FAILED").arg(rendered).arg(images));
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
