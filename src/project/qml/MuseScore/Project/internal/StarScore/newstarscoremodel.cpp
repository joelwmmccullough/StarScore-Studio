/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "newstarscoremodel.h"

#include <QSettings>

#include "translation.h"
#include "log.h"

using namespace mu::project;

// Remembered between runs so the next New starts from the last choices
static const char* SETTING_ARRANGEMENT = "StarScore/newArrangement";
static const char* SETTING_DOUBLER = "StarScore/newDoubler";
static const char* SETTING_LOW_HORN = "StarScore/newLowHorn";

//! The arrangements File › New offers, in menu order. Big Band, Marching Band and Orchestra (and 1-Horn) are made
//! from the StarScore bar's Add arrangement menu instead, so they're not here.
static const QStringList NEW_ARRANGEMENTS {
    "2-horn-standard", "2-horn-any", "3-horn-standard", "3-horn-any", "4-horn-standard", "5-horn-standard", "6-horn-standard",
    "7-horn-standard",
};

//! Horn count of a Standard arrangement key ("5-horn-standard" -> 5); 0 for anything else
static int standardHorns(const QString& arrangementKey)
{
    if (!arrangementKey.endsWith("-horn-standard")) {
        return 0;
    }
    return arrangementKey.left(arrangementKey.indexOf('-')).toInt();
}

NewStarScoreModel::NewStarScoreModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList NewStarScoreModel::arrangementTemplates() const
{
    QVariantList list;
    const std::vector<StarScoreArrangementTemplate> all = starScore()->arrangementTemplates();
    for (const QString& key : NEW_ARRANGEMENTS) {
        for (const StarScoreArrangementTemplate& t : all) {
            if (t.key != key) {
                continue;
            }
            // "(recommended)" is dropdown text only; the arrangement keeps its name
            const QString text = key == "3-horn-standard" ? t.name + muse::qtrc("starscore", " (recommended)") : t.name;
            list << QVariantMap { { "text", text }, { "value", t.key } };
        }
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

QString NewStarScoreModel::lastArrangementKey() const
{
    const QString key = QSettings().value(SETTING_ARRANGEMENT, "3-horn-standard").toString();
    return NEW_ARRANGEMENTS.contains(key) ? key : QString("3-horn-standard");
}

QString NewStarScoreModel::lastDoublerId() const
{
    const QString id = QSettings().value(SETTING_DOUBLER).toString();
    for (const StarScoreHornChoice& c : starScore()->doublerChoices()) {
        if (c.instrumentId == id) {
            return id;
        }
    }
    return defaultDoublerId(lastArrangementKey());
}

QString NewStarScoreModel::lastLowHornId() const
{
    const QString id = QSettings().value(SETTING_LOW_HORN).toString();
    for (const StarScoreHornChoice& c : starScore()->lowHornChoices()) {
        if (c.instrumentId == id) {
            return id;
        }
    }
    return "bass-trombone";
}

bool NewStarScoreModel::hasDoubler(const QString& arrangementKey) const
{
    return standardHorns(arrangementKey) >= 3;
}

bool NewStarScoreModel::hasLowHorn(const QString& arrangementKey) const
{
    return standardHorns(arrangementKey) == 7;
}

QString NewStarScoreModel::defaultDoublerId(const QString& arrangementKey, bool matchSong) const
{
    // Adding to a song: what the doubler already plays in its other 3- to 7-Horn sections (Joel, 5 Oct 2026)
    const int horns = standardHorns(arrangementKey);
    if (matchSong && horns >= 3 && horns <= 7) {
        const QString inSong = starScore()->songDoublerInstrumentId();
        if (!inSong.isEmpty()) {
            return inSong;
        }
    }
    // 3/4/5-Horn: the doubler is the alto chair; 6/7-Horn: the alto is a player of its own and the doubler the soprano chair
    return standardHorns(arrangementKey) >= 6 ? QString("soprano-saxophone") : QString("alto-saxophone");
}

QVariantList NewStarScoreModel::doublerChoices(const QString& arrangementKey, bool matchSong) const
{
    // The doubler's name comes from the band roster at run time (the program never holds band members' names)
    const QString who = starScore()->rosterDoublerName();
    const int horns = standardHorns(arrangementKey);
    // Alto Sax is recommended for 3/4/5-Horn; 6/7-Horn start on Soprano Sax without calling it recommended. Adding to a
    // song: what the doubler plays in its other 3- to 7-Horn sections is the recommended one, for 3- to 7-Horn alike.
    const QString inSong = matchSong && horns >= 3 && horns <= 7 ? starScore()->songDoublerInstrumentId() : QString();
    const QString recommended = !inSong.isEmpty() ? inSong
                                : horns >= 3 && horns <= 5 ? QString("alto-saxophone") : QString();
    QVariantList list;
    for (const StarScoreHornChoice& c : starScore()->doublerChoices()) {
        QString text = who.isEmpty() ? c.bandName : muse::qtrc("starscore", "%1 on %2").arg(who, c.bandName);
        if (c.instrumentId == recommended) {
            text += muse::qtrc("starscore", " (recommended)");
        }
        list << QVariantMap { { "text", text }, { "value", c.instrumentId } };
    }
    return list;
}

QVariantList NewStarScoreModel::lowHornChoices(const QString& doublerInstrumentId) const
{
    QVariantList list;
    for (const StarScoreHornChoice& c : starScore()->lowHornChoices()) {
        if (doublerInstrumentId == "bb-bass-clarinet" && c.instrumentId == "bb-bass-clarinet") {
            continue;
        }
        QString text = c.bandName;
        if (c.instrumentId == "bass-trombone") {
            text += muse::qtrc("starscore", " (recommended)");
        }
        list << QVariantMap { { "text", text }, { "value", c.instrumentId } };
    }
    return list;
}

bool NewStarScoreModel::create(const QVariantMap& o)
{
    StarScoreNewOptions options;
    options.title = o.value("title").toString();
    options.subtitle = o.value("subtitle").toString();
    options.composer = o.value("composer").toString();
    options.keyFifths = o.value("keyFifths", 0).toInt();
    options.timeSigNumerator = o.value("timeSigNumerator", 4).toInt();
    options.timeSigDenominator = o.value("timeSigDenominator", 4).toInt();
    options.tempoBpm = o.value("tempoBpm", 120).toInt();
    options.measures = o.value("measures", 32).toInt();
    options.arrangementTemplateKey = o.value("arrangementTemplateKey", "3-horn-standard").toString();
    // only the choices the arrangement has: a doubler for Standard 3+ horns, a 7th horn for 7-Horn Standard
    if (hasDoubler(options.arrangementTemplateKey)) {
        options.doublerInstrumentId = o.value("doublerInstrumentId").toString();
    }
    if (hasLowHorn(options.arrangementTemplateKey)) {
        options.lowHornInstrumentId = o.value("lowHornInstrumentId").toString();
    }

    muse::Ret ret = starScore()->newStarScore(options);
    if (!ret) {
        LOGE() << ret.toString();
        return false;
    }

    // Remembered for the next New (the doubler and 7th horn as chosen, even when this arrangement didn't use them)
    QSettings settings;
    settings.setValue(SETTING_ARRANGEMENT, options.arrangementTemplateKey);
    if (const QString d = o.value("doublerInstrumentId").toString(); !d.isEmpty()) {
        settings.setValue(SETTING_DOUBLER, d);
    }
    if (const QString l = o.value("lowHornInstrumentId").toString(); !l.isEmpty()) {
        settings.setValue(SETTING_LOW_HORN, l);
    }
    return true;
}

QString NewStarScoreModel::arrangementName(const QString& arrangementKey) const
{
    for (const StarScoreArrangementTemplate& t : starScore()->arrangementTemplates()) {
        if (t.key == arrangementKey) {
            return t.name;
        }
    }
    return arrangementKey;
}

bool NewStarScoreModel::asksOnAdd(const QString& arrangementKey) const
{
    if (!hasDoubler(arrangementKey)) {
        return false;   // 2-Horn, Flexible, Big Band, Orchestra, Marching Band: nothing to choose
    }
    // the horn section ("3-horn" of "3-horn-standard") is made only when the score hasn't got one
    const QString hornKey = QString("%1-horn").arg(standardHorns(arrangementKey));
    for (const StarScoreSection& s : starScore()->sections()) {
        if (s.templateKey == hornKey) {
            return false;
        }
    }
    return true;
}

bool NewStarScoreModel::addArrangement(const QString& arrangementKey, const QString& doublerInstrumentId,
                                       const QString& lowHornInstrumentId)
{
    const QString doubler = hasDoubler(arrangementKey) ? doublerInstrumentId : QString();
    const QString low = hasLowHorn(arrangementKey) ? lowHornInstrumentId : QString();
    const muse::RetVal<QString> ret = starScore()->createArrangementFromTemplate(arrangementKey, doubler, low);
    if (!ret.ret) {
        LOGE() << ret.ret.toString();
        return false;
    }
    // remembered like the New StarScore choices
    QSettings settings;
    if (!doublerInstrumentId.isEmpty()) {
        settings.setValue(SETTING_DOUBLER, doublerInstrumentId);
    }
    if (!lowHornInstrumentId.isEmpty()) {
        settings.setValue(SETTING_LOW_HORN, lowHornInstrumentId);
    }
    return true;
}
