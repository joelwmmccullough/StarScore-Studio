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
 *   bassversions  makes the 7-Horn Bass Trombone's missing stand-in versions (no questions asked)
 *   styles        applies the part styles to everything
 *   export        Export to Sheets and Demos into STARSCORE_AUTOTEST_BAND (a copy, never the real folder)
 *   save          saves a copy of the song as saved.starscore
 * Everything is logged to log.txt.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    s.replace(QRegularExpression("[/:\\\\]"), "-");
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
    QTimer::singleShot(3000, [this, steps]() { runAutotestSteps(steps, 1); });
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
    } else if (step == "bassversions") {
        static const QStringList VERSIONS { "baritone-saxophone", "bass-saxophone", "bassoon", "bb-bass-clarinet",
                                            "contrabass-clarinet", "contrabassoon", "tuba" };
        for (const StarScoreSection& s : load().sections) {
            if (s.templateKey != "7-horn") {
                continue;
            }
            for (const QString& pid : s.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (!p || !p->instrumentId().toQString().contains("bass-trombone") || s.alternates.count(pid)) {
                    continue;
                }
                QStringList have;
                for (const QString& other : s.partIds) {
                    if (const engraving::Part* a = ms->partById(ID(other))) {
                        have << a->instrumentId().toQString();
                    }
                }
                QStringList missing;
                for (const QString& v : VERSIONS) {
                    if (!have.contains(v)) {
                        missing << v;
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
    } else if (step == "save") {
        INotationProjectPtr project = globalContext()->currentProject();
        const Ret r = project ? project->save(io::path_t(autotestDir() + "/saved.starscore"), SaveMode::SaveCopy, false)
                      : make_ret(Ret::Code::InternalError);
        autotestLog(QString("  saved: %1").arg(r ? "ok" : QString::fromStdString(r.toString())));
    } else {
        autotestLog("  unknown step");
    }

    QTimer::singleShot(500, [this, steps, reportNumber]() { runAutotestSteps(steps, reportNumber); });
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
