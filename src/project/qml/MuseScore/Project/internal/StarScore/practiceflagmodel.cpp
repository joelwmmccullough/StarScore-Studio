/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — practice flags (see practiceflagmodel.h)
 */
#include "practiceflagmodel.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/editing/editscoreproperties.h"
#include "notation/imasternotation.h"
#include "notation/inotationelements.h"
#include "notation/inotation.h"
#include "types/translatablestring.h"

using namespace mu::project;

static const char16_t* FLAGS_TAG = u"starscoreFlags";

PracticeFlagModel::PracticeFlagModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QJsonArray PracticeFlagModel::read() const
{
    notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms) {
        return {};
    }
    return QJsonDocument::fromJson(ms->metaTag(FLAGS_TAG).toQString().toUtf8()).object().value("flags").toArray();
}

void PracticeFlagModel::write(const QJsonArray& flags, const QString& undoText)
{
    notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms) {
        return;
    }
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject { { "flags", flags } }).toJson(QJsonDocument::Compact));
    notation::INotationUndoStackPtr stack = master->notation()->undoStack();
    stack->prepareChanges(muse::TranslatableString::untranslatable(undoText));
    ms->undo(new engraving::ChangeMetaText(ms, muse::String(FLAGS_TAG), muse::String::fromQString(json)));
    stack->commitChanges();
    emit changed();
}

QVariantList PracticeFlagModel::flags() const
{
    static const QStringList DIFF { "", "Easy", "Medium", "Hard", "Virtuosic" };
    static const QMap<QString, QString> KIND { { "solo", "Solo" }, { "comping", "Comping" }, { "parts", "Written part" } };
    QVariantList out;
    for (const QJsonValue& v : read()) {
        QVariantMap m = v.toObject().toVariantMap();
        const int d = std::clamp(m.value("difficulty").toInt(), 1, 4);
        m["difficultyName"] = DIFF[d];
        m["kindName"] = KIND.value(m.value("kind").toString(), m.value("kind").toString());
        m["bars"] = m.value("from").toInt() == m.value("to").toInt() ? QString("Bar %1").arg(m.value("from").toInt())
                    : QString("Bars %1–%2").arg(m.value("from").toInt()).arg(m.value("to").toInt());
        out << m;
    }
    return out;
}

void PracticeFlagModel::setFromBar(int b)
{
    m_from = std::max(1, b);
    m_to = std::max(m_to, m_from);
    emit changed();
}

void PracticeFlagModel::setToBar(int b)
{
    m_to = std::max(m_from, b);
    emit changed();
}

void PracticeFlagModel::useSelection()
{
    notation::INotationPtr n = globalContext()->currentNotation();
    engraving::Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    m_message.clear();
    if (!score) {
        m_message = QStringLiteral("Open a song first.");
        emit changed();
        return;
    }
    m_part = score->isMaster() ? QStringLiteral("Full score") : n->name();
    const engraving::Selection& sel = score->selection();
    const engraving::Measure* a = nullptr;
    const engraving::Measure* b = nullptr;
    if (sel.isRange()) {
        a = score->tick2measure(sel.tickStart());
        b = score->tick2measure(sel.tickEnd() - engraving::Fraction(1, 1920));
    } else {
        for (const engraving::EngravingItem* e : sel.elements()) {
            const engraving::Measure* m = e ? e->findMeasure() : nullptr;
            if (m && (!a || m->tick() < a->tick())) {
                a = m;
            }
            if (m && (!b || m->tick() > b->tick())) {
                b = m;
            }
        }
    }
    if (!a || !b) {
        m_message = QStringLiteral("Select the bars first (click a bar, then Shift+click the last one), or type the bar numbers.");
        emit changed();
        return;
    }
    m_from = a->no() + 1;
    m_to = std::max(m_from, b->no() + 1);
    emit changed();
}

bool PracticeFlagModel::save(const QString& id, const QString& kind, int difficulty, const QString& text)
{
    if (m_from < 1 || m_to < m_from) {
        m_message = QStringLiteral("Choose the bars first.");
        emit changed();
        return false;
    }
    QJsonArray flags = read();
    QJsonObject f;
    int at = -1;
    for (int i = 0; i < flags.size(); ++i) {
        if (!id.isEmpty() && flags[i].toObject().value("id").toString() == id) {
            at = i;
            f = flags[i].toObject();
        }
    }
    f["id"] = id.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch(), 36) : id;
    f["from"] = m_from;
    f["to"] = m_to;
    f["kind"] = kind;
    f["difficulty"] = std::clamp(difficulty, 1, 4);
    f["text"] = text.trimmed();
    f["part"] = m_part;
    f["date"] = QDate::currentDate().toString(Qt::ISODate);
    if (at >= 0) {
        flags[at] = f;
    } else {
        flags.append(f);
    }
    write(flags, "Practice flag");
    m_message = QStringLiteral("Saved. It's in this song's practice items once the song is saved.");
    emit changed();
    return true;
}

void PracticeFlagModel::remove(const QString& id)
{
    QJsonArray flags = read();
    for (int i = 0; i < flags.size(); ++i) {
        if (flags[i].toObject().value("id").toString() == id) {
            flags.removeAt(i);
            write(flags, "Remove practice flag");
            return;
        }
    }
}
