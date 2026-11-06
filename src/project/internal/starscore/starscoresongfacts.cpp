/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a song's facts (see starscoresongfacts.h)
 */
#include "starscoresongfacts.h"

#include <cmath>

#include <QStringList>

#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempo.h"

using namespace mu::engraving;

namespace mu::project::starscore {
namespace {
QString keyName(int fifths, bool minor)
{
    // the tonic, round the circle of fifths from C (major) or A (minor)
    static const char* MAJOR[15] = { "C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C", "G", "D", "A", "E", "B", "F♯", "C♯" };
    static const char* MINOR[15] = { "A♭", "E♭", "B♭", "F", "C", "G", "D", "A", "E", "B", "F♯", "C♯", "G♯", "D♯", "A♯" };
    if (fifths < -7 || fifths > 7) {
        return QString();
    }
    return QString::fromUtf8(minor ? MINOR[fifths + 7] : MAJOR[fifths + 7]) + (minor ? " minor" : " major");
}
}

QString SongFacts::line() const
{
    QStringList parts;
    if (!key.isEmpty()) {
        parts << key;
    }
    if (!timeSig.isEmpty()) {
        parts << timeSig;
    }
    if (bpm > 0) {
        parts << QString::fromUtf8("♩ = ") + QString::number(bpm);
    }
    if (bars > 0) {
        parts << QString("%1 bars").arg(bars);
    }
    if (seconds > 0) {
        parts << QString("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    }
    return parts.join(QString::fromUtf8("  ·  "));
}

QVariantMap SongFacts::toMap() const
{
    return QVariantMap { { "key", key }, { "timeSig", timeSig }, { "bpm", bpm }, { "bars", bars }, { "seconds", seconds } };
}

SongFacts SongFacts::fromMap(const QVariantMap& m)
{
    SongFacts f;
    f.key = m.value("key").toString();
    f.timeSig = m.value("timeSig").toString();
    f.bpm = m.value("bpm").toInt();
    f.bars = m.value("bars").toInt();
    f.seconds = m.value("seconds").toInt();
    return f;
}

SongFacts songFacts(MasterScore* ms)
{
    SongFacts f;
    if (!ms || !ms->firstMeasure()) {
        return f;
    }
    const Fraction start = Fraction(0, 1);
    if (ms->nstaves() > 0) {
        const KeySigEvent ev = ms->staff(0)->keySigEvent(start);
        const int fifths = int(ev.concertKey());
        bool minor = ev.mode() == KeyMode::MINOR || ev.mode() == KeyMode::AEOLIAN;
        if (ev.mode() == KeyMode::UNKNOWN || ev.mode() == KeyMode::NONE) {
            // (most key signatures don't say: a song whose first chord symbol is the relative minor's minor chord is
            // taken to be in the minor key, "A minor" for a song with no sharps or flats starting on Am)
            for (Segment* seg = ms->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                const Harmony* h = nullptr;
                for (EngravingItem* a : seg->annotations()) {
                    if (a && a->isHarmony()) {
                        h = toHarmony(a);
                        break;
                    }
                }
                if (!h || h->chords().empty()) {
                    continue;
                }
                const int root = h->chords().front()->rootTpc();
                const QString suffix = h->chords().front()->textName().toQString();
                const int majorTonic = ((7 * fifths) % 12 + 12) % 12;
                const int minorTonic = (majorTonic + 9) % 12;
                const bool minorChord = (suffix.startsWith('m') && !suffix.startsWith("ma") && !suffix.startsWith("mix"))
                                        || suffix.startsWith('-') || suffix.startsWith("min");
                minor = root != Tpc::TPC_INVALID && (tpc2pitch(root) % 12 + 12) % 12 == minorTonic && minorChord;
                break;
            }
        }
        f.key = keyName(fifths, minor);
    }
    const TimeSigFrac ts = ms->sigmap()->timesig(0).nominal();   // (the written one, not a pickup's length)
    if (ts.numerator() > 0) {
        f.timeSig = QString("%1/%2").arg(ts.numerator()).arg(ts.denominator());
    }
    if (ms->tempomap()) {
        // (the tempo map's beats are quarter notes; its multiplier is playback's speed, which the times include: taken
        // out again)
        const double mult = ms->tempomap()->tempoMultiplier().val > 0 ? ms->tempomap()->tempoMultiplier().val : 1.0;
        f.bpm = int(std::lround(ms->tempomap()->tempo(0).val * 60.0));
        const RepeatList& repeats = ms->repeatList(true, false);   // (repeats played out; the ties left as they are)
        const int ticks = repeats.ticks();
        if (ticks > 0) {
            f.seconds = int(std::lround(repeats.utick2utime(ticks) * mult));
        }
    }
    f.bars = int(ms->nmeasures());
    return f;
}
}
