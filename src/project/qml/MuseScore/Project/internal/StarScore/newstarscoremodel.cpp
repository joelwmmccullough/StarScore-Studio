/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "newstarscoremodel.h"

#include "translation.h"
#include "log.h"

using namespace mu::project;

NewStarScoreModel::NewStarScoreModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList NewStarScoreModel::arrangementTemplates() const
{
    QVariantList list;
    for (const StarScoreArrangementTemplate& t : starScore()->arrangementTemplates()) {
        list << QVariantMap { { "text", t.name }, { "value", t.key } };
    }
    return list;
}

QVariantList NewStarScoreModel::keys() const
{
    static const char* names[] = {
        "C♭ major / A♭ minor", "G♭ major / E♭ minor", "D♭ major / B♭ minor", "A♭ major / F minor",
        "E♭ major / C minor", "B♭ major / G minor", "F major / D minor", "C major / A minor",
        "G major / E minor", "D major / B minor", "A major / F♯ minor", "E major / C♯ minor",
        "B major / G♯ minor", "F♯ major / D♯ minor", "C♯ major / A♯ minor"
    };

    QVariantList list;
    for (int fifths = -7; fifths <= 7; ++fifths) {
        list << QVariantMap { { "text", QString::fromUtf8(names[fifths + 7]) }, { "value", fifths } };
    }
    return list;
}

bool NewStarScoreModel::create(const QVariantMap& o)
{
    StarScoreNewOptions options;
    options.title = o.value("title").toString();
    options.composer = o.value("composer").toString();
    options.keyFifths = o.value("keyFifths", 0).toInt();
    options.timeSigNumerator = o.value("timeSigNumerator", 4).toInt();
    options.timeSigDenominator = o.value("timeSigDenominator", 4).toInt();
    options.tempoBpm = o.value("tempoBpm", 120).toInt();
    options.measures = o.value("measures", 32).toInt();
    options.arrangementTemplateKey = o.value("arrangementTemplateKey", "3-horn-standard").toString();

    muse::Ret ret = starScore()->newStarScore(options);
    if (!ret) {
        LOGE() << ret.toString();
    }
    return bool(ret);
}
