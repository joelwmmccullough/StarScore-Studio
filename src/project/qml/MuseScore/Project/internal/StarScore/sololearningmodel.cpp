/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Dashboard › Solo transcriptions (1.25.0)
 */
#include "sololearningmodel.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include "translation.h"
#include "log.h"

using namespace mu::project;

SoloLearningModel::SoloLearningModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList SoloLearningModel::solos() const
{
    return m_solos;
}

int SoloLearningModel::keysMemorized() const
{
    return m_memorized;
}

int SoloLearningModel::keysTotal() const
{
    return m_total;
}

void SoloLearningModel::load()
{
    if (!m_listening) {
        m_listening = true;
        starScore()->soloLearningChanged().onNotify(this, [this]() { load(); });
    }
    m_solos.clear();
    m_memorized = 0;
    m_total = 0;
    for (const QJsonValue& v : starScore()->soloLearningList()) {
        QVariantMap solo = v.toObject().toVariantMap();
        int learned = 0;
        int memorized = 0;
        const QVariantList keys = solo.value("keys").toList();
        for (const QVariant& k : keys) {
            const QString state = k.toMap().value("state").toString();
            learned += state == "learned" ? 1 : 0;
            memorized += state == "memorized" ? 1 : 0;
        }
        solo["learned"] = learned;
        solo["memorized"] = memorized;
        m_memorized += memorized;
        m_total += int(keys.size());
        m_solos << solo;
    }
    emit changed();
}

void SoloLearningModel::cycleKey(const QString& id, int keyIndex)
{
    for (const QVariant& s : m_solos) {
        const QVariantMap solo = s.toMap();
        if (solo.value("id").toString() != id) {
            continue;
        }
        for (const QVariant& k : solo.value("keys").toList()) {
            const QVariantMap key = k.toMap();
            if (key.value("index").toInt() == keyIndex) {
                const QString now = key.value("state").toString();
                setKeyState(id, keyIndex, now.isEmpty() ? QString("learned") : now == "learned" ? QString("memorized") : QString());
                return;
            }
        }
    }
}

void SoloLearningModel::setKeyState(const QString& id, int keyIndex, const QString& state)
{
    const muse::Ret ret = starScore()->setSoloKeyState(id, keyIndex, state);
    if (!ret) {
        interactive()->error(muse::trc("starscore", "Couldn't mark the key"), ret.text().empty() ? ret.toString() : ret.text());
    }
    // (the service's notification reloads the list)
}

void SoloLearningModel::playAudio(const QString& soloPath, const QString& idOrPath)
{
    // (1.26.0) stored inside the solo: unpacked for the moment (a Transcribe! file opens the solo first)
    const muse::Ret ret = starScore()->openScoreAudio(soloPath, idOrPath);
    if (!ret) {
        interactive()->error(muse::trc("starscore", "Couldn't open the file"), ret.text().empty() ? ret.toString() : ret.text());
    }
}

void SoloLearningModel::addAudio(const QString& soloPath)
{
    const muse::RetVal<QString> added = starScore()->addScoreAudio(soloPath);
    if (!added.ret) {
        interactive()->error(muse::trc("starscore", "Couldn't add the file"),
                             added.ret.text().empty() ? added.ret.toString() : added.ret.text());
    }
    // (the service's notification reloads the list)
}

void SoloLearningModel::openSolo(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    auto d = dispatcher();
    QTimer::singleShot(0, qApp, [d, path]() {
        d->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    });
}
