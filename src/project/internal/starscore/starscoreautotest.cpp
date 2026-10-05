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
 *   export        Export to Sheets and Demos into STARSCORE_AUTOTEST_BAND (a copy, never the real folder)
 *   chords        the chord charts' three outputs: chords.json (the lead sheet's form and chords, extract.py's
 *                 JSON), chart.html (the PDF page, without the version footer, as pdfchart.py writes it),
 *                 ireal.html (the iReal Pro page); plus CODE - Chord Chart.html with the footer. The page's font
 *                 URLs come from STARSCORE_AUTOTEST_FONTS ("modernoir;jost;bravura"), else the app's embedded ones.
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
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/text.h"
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
    autotestLog("step: " + step);
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
                QStringList have;
                for (const QString& other : s.partIds) {
                    if (const engraving::Part* a = ms->partById(ID(other))) {
                        have << starscore::bandHornName(a->instrumentId().toQString());
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
    } else if (step.startsWith("home")) {
        // home or home:<section>: the Home page (for a screenshot)
        const QString section = step.section(':', 1);
        interactive()->open(UriQuery(section.isEmpty() ? std::string("musescore://home")
                                     : ("musescore://home?section=" + section.toStdString())));
        autotestLog("  home " + section);
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
    } else if (step == "save") {
        INotationProjectPtr project = globalContext()->currentProject();
        const Ret r = project ? project->save(io::path_t(autotestDir() + "/saved.starscore"), SaveMode::SaveCopy, false)
                      : make_ret(Ret::Code::InternalError);
        autotestLog(QString("  saved: %1").arg(r ? "ok" : QString::fromStdString(r.toString())));
    } else {
        autotestLog("  unknown step");
    }

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
