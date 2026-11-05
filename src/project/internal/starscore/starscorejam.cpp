/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Jam: the backing band (see starscorejam.h). jamForm() is in starscorechordchart.cpp, beside the
 * chord chart it reads.
 *
 * The grooves are written here, one per style, bar by bar: the bass walks (swing) or plays the style's pattern from the
 * chord's root, third, fifth and seventh, leading into the next chord by a half step; the piano comps rootless voicings
 * (third, seventh, ninth, fifth or thirteenth) kept close to the voicing before; the drums play the style's pattern with
 * a fill at the end of every eighth bar and of the form, and a crash at the top of each chorus. A beat is a quarter
 * note, or a dotted quarter in 6/8, 9/8 and 12/8 (whose offbeats then fall on the third triplet: swung by themselves).
 * The variations are random with a fixed seed, so the same settings always play the same.
 */
#include "starscorejam.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

#include <QRegularExpression>

#include "mpe/events.h"
#include "mpe/playbacksetupdata.h"
#include "playback/playbacktypes.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;

namespace mu::project::starscore {
namespace {
// General MIDI drum sounds
constexpr int KICK = 36, SIDESTICK = 37, SNARE = 38, HAT = 42, PEDAL_HAT = 44, OPEN_HAT = 46, LOW_TOM = 45, MID_TOM = 47,
              HIGH_TOM = 50, RIDE = 51, CRASH = 49, BELL = 53, SHAKER = 70, COWBELL = 56;

struct Groove {
    QString id;
    QString name;
    bool swung = false;
};

const std::vector<Groove>& grooves()
{
    static const std::vector<Groove> list {
        { "swing", "Swing", true },
        { "ballad", "Ballad", true },
        { "bossa", "Bossa Nova", false },
        { "latin", "Latin", false },
        { "samba", "Samba", false },
        { "funk", "Funk", false },
        { "rock", "Rock / Pop", false },
        { "waltz", "Jazz Waltz", true },
        { "shuffle", "Shuffle / Boogie", true },
    };
    return list;
}

bool isSwung(const QString& groove)
{
    for (const Groove& g : grooves()) {
        if (g.id == groove) {
            return g.swung;
        }
    }
    return false;
}

int mod12(int x)
{
    return ((x % 12) + 12) % 12;
}

//! a note of pitch class pc nearest to target
int nearest(int pc, int target)
{
    int p = target - mod12(target - pc);
    if (target - p > 6) {
        p += 12;
    }
    return p;
}

//! a pitch class into a range, nearest to target inside it
int inRange(int pc, int target, int low, int high)
{
    int p = nearest(pc, target);
    while (p < low) {
        p += 12;
    }
    while (p > high) {
        p -= 12;
    }
    return p;
}
}

QStringList jamGrooves()
{
    QStringList out;
    for (const Groove& g : grooves()) {
        out << g.id;
    }
    return out;
}

QString jamGrooveName(const QString& groove)
{
    for (const Groove& g : grooves()) {
        if (g.id == groove) {
            return g.name;
        }
    }
    return groove;
}

QString jamGrooveFor(const QString& words, int beats)
{
    const QString w = words.toLower();
    auto has = [&](std::initializer_list<const char*> keys) {
        return std::any_of(keys.begin(), keys.end(), [&](const char* k) { return w.contains(QString::fromLatin1(k)); });
    };
    if (has({ "bossa" })) {
        return "bossa";
    }
    if (has({ "samba", "partido" })) {
        return "samba";
    }
    if (has({ "latin", "afro", "salsa", "mambo", "songo", "cha", "montuno", "guaguanc", "rumba", "tumba" })) {
        return "latin";
    }
    if (has({ "boogie", "shuffle", "blues" })) {
        return "shuffle";
    }
    if (has({ "ballad" })) {
        return "ballad";
    }
    if (has({ "waltz" }) || (beats == 3 && has({ "swing", "jazz" }))) {
        return "waltz";
    }
    if (has({ "swing", "bop", "jazz", "up tempo", "medium", "walk" })) {
        return beats == 3 ? "waltz" : "swing";
    }
    if (has({ "rock", "pop", "even", "straight" })) {
        return "rock";
    }
    if (has({ "funk", "groove", "fusion", "hip", "soul", "r&b", "gospel" })) {
        return "funk";
    }
    return beats == 3 ? "waltz" : "funk";
}

std::vector<int> jamChordTones(const QString& qualityIn)
{
    const QString q = QString(qualityIn).remove(' ').remove('(').remove(')').remove(',');
    const QString lq = q.toLower();
    int third = 4, fifth = 7, seventh = -1;
    std::vector<int> ext;
    const bool major7 = q.contains("maj") || q.contains("ma7") || q.contains("ma9") || q.contains("M7") || q.contains("M9")
                        || q.contains("^") || q.contains(QChar(0x0394)) /* Δ */ || q.contains(QChar(0x2206));
    const bool halfDim = q.contains(QChar(0x00F8)) /* ø */ || q.contains(QChar(0x00D8)) || lq.contains("m7b5") || lq.contains("mi7b5")
                         || lq.contains("-7b5") || lq.startsWith("h");
    const bool dim = !halfDim && (lq.startsWith("dim") || lq.startsWith("o") || q.startsWith(QChar(0x00B0)));
    const bool minor = !major7 ? (lq.startsWith("m") || lq.startsWith("-") || lq.startsWith("min") || lq.startsWith("mi"))
                       : (lq.startsWith("m") && !lq.startsWith("ma") && !q.startsWith("M")) || lq.startsWith("-");
    if (halfDim) {
        third = 3;
        fifth = 6;
        seventh = 10;
    } else if (dim) {
        third = 3;
        fifth = 6;
        seventh = q.contains('7') ? 9 : -1;
    } else if (minor) {
        third = 3;
    }
    if (lq.contains("sus")) {
        third = lq.contains("sus2") ? 2 : 5;
    }
    if (lq.contains("aug") || q.startsWith('+') || lq.contains("#5") || lq.contains("+5")) {
        fifth = 8;
    } else if (!halfDim && !dim && lq.contains("b5")) {
        fifth = 6;
    }
    const bool alt = lq.contains("alt");
    const bool six = q.contains('6') && !q.contains("13") && !q.contains("b6");
    if (seventh < 0) {
        if (major7) {
            seventh = 11;
        } else if (alt || q.contains('7') || q.contains('9') || q.contains("11") || q.contains("13")) {
            seventh = six && !q.contains('7') ? 9 : 10;
        } else if (six) {
            seventh = 9;
        }
    }
    if (alt) {
        ext = { 13, 15, 20 };
        fifth = -1;
    } else {
        if (lq.contains("b9")) {
            ext.push_back(13);
        } else if (lq.contains("#9")) {
            ext.push_back(15);
        } else if (q.contains('9') || q.contains("11") || (q.contains("13") && seventh != 9)) {
            ext.push_back(14);
        }
        if (lq.contains("#11")) {
            ext.push_back(18);
        } else if (q.contains("11") && third != 4) {
            ext.push_back(17);
        }
        if (lq.contains("b13")) {
            ext.push_back(20);
        } else if (q.contains("13")) {
            ext.push_back(21);
        }
    }
    std::vector<int> out { 0, third };
    if (fifth >= 0) {
        out.push_back(fifth);
    }
    if (seventh >= 0) {
        out.push_back(seventh);
    }
    for (int e : ext) {
        out.push_back(e);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Making the jam
// ---------------------------------------------------------------------------------------------------------------------

namespace {
struct Harmony {
    int root = 0;
    int bass = 0;           // what the bass plays as the root (a slash chord's bass)
    std::vector<int> tones; // above the root
    int third() const { return tones.size() > 1 ? tones[1] : 4; }
    int fifth() const
    {
        for (int t : tones) {
            if (t == 6 || t == 7 || t == 8) {
                return t;
            }
        }
        return 7;
    }
    int seventh() const
    {
        for (int t : tones) {
            if (t == 9 || t == 10 || t == 11) {
                return t;
            }
        }
        return -1;
    }
};

class Maker
{
public:
    Maker(const JamForm& form, const JamSettings& s)
        : m_form(form), m_s(s), m_rng(s.seed)
    {
        m_swung = isSwung(s.groove);
    }

    Jam make();

private:
    struct BarCtx {
        const JamBar* bar = nullptr;
        double start = 0;        // seconds
        double secPerQuarter = 0.5;
        int beats = 4;           // felt beats
        double beatQ = 1.0;      // a beat in quarters
        bool compound = false;
        int formBar = 0;
        int chorus = 0;
        bool phraseEnd = false;  // a fill here
        bool formStart = false;
        bool countIn = false;
    };

    Harmony harmonyAt(int formBar, double quarter) const;
    double timeOf(const BarCtx& b, double beat) const;   // beat (in felt beats, from the bar's start) -> seconds
    void note(JamTrack& t, double start, double len, int pitch, int vel);
    int vel(int base, int spread = 8);
    bool chance(double p);

    void bass(const BarCtx& b, const BarCtx* next);
    void drums(const BarCtx& b);
    void piano(const BarCtx& b, const BarCtx* next);
    void comp(const BarCtx& b, double beat, double lenBeats, int v);

    const JamForm& m_form;
    JamSettings m_s;
    std::mt19937 m_rng;
    bool m_swung = false;
    JamTrack m_bass { "bass", {} };
    JamTrack m_drums { "drums", {} };
    JamTrack m_piano { "piano", {} };
    int m_lastBass = 40;
    std::vector<int> m_lastVoicing;
};

int Maker::vel(int base, int spread)
{
    std::uniform_int_distribution<int> d(-spread, spread);
    return std::clamp(base + d(m_rng), 1, 127);
}

bool Maker::chance(double p)
{
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(m_rng) < p;
}

void Maker::note(JamTrack& t, double start, double len, int pitch, int v)
{
    if (len > 0.005 && pitch >= 0 && pitch <= 127) {
        t.notes.push_back({ start, len, pitch, v });
    }
}

Harmony Maker::harmonyAt(int formBar, double quarter) const
{
    // the last chord at or before this point, looking back through the bars before when the bar has none yet
    const JamChord* found = nullptr;
    for (int b = formBar; b >= 0 && !found; --b) {
        const auto& chords = m_form.bars[size_t(b)].chords;
        for (const JamChord& c : chords) {
            if (b < formBar || c.at <= quarter + 1e-6) {
                found = &c;
            }
        }
        quarter = 1e9;
    }
    if (!found) {
        // before the first chord: the first one
        for (const JamBar& bar : m_form.bars) {
            if (!bar.chords.empty()) {
                found = &bar.chords.front();
                break;
            }
        }
    }
    Harmony h;
    if (found) {
        h.root = mod12(found->root + m_s.transpose);
        h.bass = found->bass >= 0 ? mod12(found->bass + m_s.transpose) : h.root;
        h.tones = jamChordTones(found->quality);
    } else {
        h.tones = { 0, 4, 7 };
    }
    return h;
}

double Maker::timeOf(const BarCtx& b, double beat) const
{
    double q = beat;
    if (m_swung && !b.compound) {
        // a swung eighth: the offbeat late, triplet-like, a little straighter when it's fast
        const double whole = std::floor(beat + 1e-9);
        const double frac = beat - whole;
        if (std::abs(frac - 0.5) < 1e-6) {
            const double bpm = 60.0 / b.secPerQuarter;
            q = whole + (bpm > 220 ? 0.58 : bpm > 160 ? 0.62 : 0.66);
        }
    }
    return b.start + q * b.beatQ * b.secPerQuarter;
}

Jam Maker::make()
{
    Jam jam;
    const int n = int(m_form.bars.size());
    if (n == 0) {
        return jam;
    }
    const int from = m_s.fromBar >= 0 ? std::clamp(m_s.fromBar, 0, n - 1) : 0;
    const int to = m_s.toBar >= 0 ? std::clamp(m_s.toBar, from, n - 1) : n - 1;
    const int choruses = std::max(1, m_s.choruses);

    std::vector<BarCtx> plan;
    double t = 0;
    auto feel = [](BarCtx& c, const JamBar& bar) {
        // a beat: a quarter note, or a dotted quarter in 6/8, 9/8 and 12/8 (5/8 and 7/8 are felt in quarters, the
        // bar's odd eighth left to the bar's last beat)
        c.compound = bar.beatType == 8 && bar.beats % 3 == 0 && bar.beats >= 6;
        c.beatQ = c.compound ? 1.5 : 1.0;
        c.beats = std::max(1, int(std::floor(bar.quarters / c.beatQ + 1e-6)));
    };
    // the count-in: the first bar's meter, the first chorus's tempo
    for (int i = 0; i < std::max(0, m_s.countInBars); ++i) {
        BarCtx c;
        c.bar = &m_form.bars[size_t(from)];
        c.secPerQuarter = 60.0 / std::max(20, m_s.bpm);
        c.start = t;
        c.countIn = true;
        c.formBar = from;
        feel(c, *c.bar);
        plan.push_back(c);
        t += c.bar->quarters * c.secPerQuarter;
    }
    for (int ch = 1; ch <= choruses; ++ch) {
        const int bpm = std::clamp(m_s.bpm + m_s.tempoStep * (ch - 1), 20, 400);
        for (int b = from; b <= to; ++b) {
            BarCtx c;
            c.bar = &m_form.bars[size_t(b)];
            c.secPerQuarter = 60.0 / bpm;
            c.start = t;
            c.formBar = b;
            c.chorus = ch;
            c.formStart = b == from;
            const int inForm = b - from + 1;
            c.phraseEnd = b == to || (inForm % 8 == 0 && to - from + 1 > 8);
            feel(c, *c.bar);
            plan.push_back(c);
            t += c.bar->quarters * c.secPerQuarter;
        }
    }
    // a last downbeat to end on
    BarCtx last;
    last.bar = &m_form.bars[size_t(from)];
    last.secPerQuarter = plan.back().secPerQuarter;
    last.start = t;
    last.formBar = from;
    feel(last, *last.bar);

    for (size_t i = 0; i < plan.size(); ++i) {
        const BarCtx& b = plan[i];
        const BarCtx* next = i + 1 < plan.size() ? &plan[i + 1] : nullptr;
        jam.bars.push_back({ b.start, b.chorus, b.formBar });
        if (b.countIn) {
            // sticks on the beats
            for (int k = 0; k < b.beats; ++k) {
                note(m_drums, timeOf(b, k), 0.1, SIDESTICK, k == 0 ? 100 : 80);
            }
            continue;
        }
        if (m_s.drums) {
            drums(b);
        }
        if (m_s.bass) {
            bass(b, next);
        }
        if (m_s.piano) {
            piano(b, next);
        }
    }
    // the ending: the top of the form's first chord, held
    const Harmony end = harmonyAt(from, 0);
    const double hold = 2.0 * last.secPerQuarter * last.beatQ;
    if (m_s.drums) {
        note(m_drums, last.start, 1.0, CRASH, 105);
        note(m_drums, last.start, 0.3, KICK, 100);
    }
    if (m_s.bass) {
        note(m_bass, last.start, hold, inRange(end.bass, 36, 28, 47), 95);
    }
    if (m_s.piano) {
        comp(last, 0, 2.0, 80);
    }
    jam.seconds = last.start + hold + 1.5;
    jam.electricBass = m_s.groove == "funk" || m_s.groove == "rock";
    for (JamTrack* tr : { &m_bass, &m_drums, &m_piano }) {
        if (!tr->notes.empty()) {
            jam.tracks.push_back(*tr);
        }
    }
    return jam;
}

void Maker::bass(const BarCtx& b, const BarCtx* next)
{
    const QString& g = m_s.groove;
    auto rootAt = [&](double beat) { return harmonyAt(b.formBar, beat * b.beatQ); };
    auto bassNote = [&](int pc, int target) { return inRange(pc, target, 28, 50); };
    const Harmony nextH = next && !next->countIn ? harmonyAt(next->formBar, 0) : rootAt(0);

    if (g == "swing" || g == "waltz" || (g == "shuffle" && b.compound)) {
        // walking quarters: the root on the beat a chord starts, chord tones between, a half step into the next chord
        for (int k = 0; k < b.beats; ++k) {
            const Harmony h = rootAt(k);
            const bool chordStarts = k == 0 || rootAt(k).root != rootAt(k - 1).root || rootAt(k).bass != rootAt(k - 1).bass;
            const bool lastBeat = k == b.beats - 1;
            const Harmony after = lastBeat ? nextH : rootAt(k + 1);
            const bool changeNext = lastBeat || after.root != h.root || after.bass != h.bass;
            int p;
            if (chordStarts) {
                p = bassNote(h.bass, m_lastBass);
            } else if (changeNext) {
                const int target = bassNote(after.bass, m_lastBass);
                p = chance(0.5) ? target + 1 : target - 1;
                if (chance(0.25)) {
                    p = bassNote(mod12(after.bass + 7), m_lastBass);   // its fifth, from above or below
                }
            } else {
                static const int choices[] = { 0, 1, 2, 3 };
                const int which = choices[std::uniform_int_distribution<int>(0, 3)(m_rng)];
                const int interval = which == 0 ? h.third() : which == 1 ? h.fifth() : which == 2 ? (h.seventh() >= 0 ? h.seventh() : 12) : 2;
                p = bassNote(mod12(h.root + interval), m_lastBass + (chance(0.5) ? 3 : -3));
            }
            const double len = (timeOf(b, k + 1) - timeOf(b, k)) * 0.92;
            note(m_bass, timeOf(b, k), len, p, vel(k == 0 ? 92 : 84));
            m_lastBass = p;
            if (g == "waltz" && k == 0 && b.beats == 3 && chance(0.3)) {
                break;   // (sometimes a dotted half in a waltz)
            }
        }
        return;
    }

    struct Hit {
        double beat;
        double len;
        int interval;   // above the bass note: 0 root, 7 fifth, 12 octave, -1 a half step below the next root
    };
    std::vector<Hit> pattern;
    if (g == "ballad") {
        pattern = { { 0, 2, 0 }, { 2, 2, 7 } };
    } else if (g == "bossa") {
        pattern = { { 0, 1.5, 0 }, { 1.5, 0.5, 7 }, { 2, 1.5, 7 }, { 3.5, 0.5, 0 } };
    } else if (g == "samba") {
        pattern = { { 0, 0.75, 0 }, { 0.75, 0.25, 0 }, { 1, 1, 7 }, { 2, 0.75, 0 }, { 2.75, 0.25, 0 }, { 3, 1, 7 } };
    } else if (g == "latin") {
        // tumbao: the and of two and four, the next chord's root pushed early
        pattern = { { 0, 0.5, 0 }, { 1.5, 1.5, 7 }, { 3, 1, -1 } };
    } else if (g == "funk") {
        static const std::vector<std::vector<Hit> > funk {
            { { 0, 0.5, 0 }, { 0.75, 0.25, 0 }, { 1.5, 0.5, 12 }, { 2.5, 0.25, 0 }, { 2.75, 0.25, 10 }, { 3, 0.5, 7 }, { 3.5, 0.25, 0 } },
            { { 0, 0.75, 0 }, { 1, 0.25, 12 }, { 1.75, 0.25, 0 }, { 2.5, 0.5, 0 }, { 3.25, 0.25, 7 }, { 3.75, 0.25, -1 } },
        };
        pattern = funk[size_t(std::uniform_int_distribution<int>(0, 1)(m_rng))];
    } else if (g == "shuffle") {
        // boogie: root, third, fifth, sixth, seventh, sixth, fifth, third in swung eighths
        pattern = { { 0, 0.5, 0 }, { 0.5, 0.5, 4 }, { 1, 0.5, 7 }, { 1.5, 0.5, 9 }, { 2, 0.5, 10 }, { 2.5, 0.5, 9 }, { 3, 0.5, 7 }, { 3.5, 0.5, 4 } };
    } else {   // rock
        pattern = { { 0, 0.5, 0 }, { 0.5, 0.5, 0 }, { 1, 0.5, 0 }, { 1.5, 0.5, 0 }, { 2, 0.5, 0 }, { 2.5, 0.5, 0 }, { 3, 0.5, 7 }, { 3.5, 0.5, 0 } };
    }
    for (const Hit& hit : pattern) {
        for (double base = 0; base < b.beats; base += 4) {
            const double beat = base + hit.beat;
            if (beat >= b.beats - 1e-6) {
                continue;
            }
            const Harmony h = rootAt(beat);
            int p;
            if (hit.interval < 0) {
                const Harmony after = beat + 1 >= b.beats ? nextH : rootAt(beat + 1);
                p = bassNote(after.bass, m_lastBass) - 1;
            } else {
                int interval = hit.interval;
                if (g == "shuffle" && (interval == 4 || interval == 3)) {
                    interval = h.third();
                }
                p = bassNote(h.bass, 38) + (interval >= 12 ? 12 : 0) + (interval % 12);
                if (interval % 12 != 0) {
                    p = bassNote(mod12(h.bass + interval), bassNote(h.bass, 38) + interval);
                }
            }
            const double len = std::min(hit.len, b.beats - beat);
            note(m_bass, timeOf(b, beat), (timeOf(b, beat + len) - timeOf(b, beat)) * 0.9, p, vel(hit.beat == 0 ? 95 : 82));
            m_lastBass = p;
        }
    }
}

void Maker::drums(const BarCtx& b)
{
    const QString& g = m_s.groove;
    auto hit = [&](double beat, int sound, int v) {
        if (beat < b.beats - 1e-6) {
            note(m_drums, timeOf(b, beat), 0.12, sound, vel(v, 6));
        }
    };
    const bool fill = b.phraseEnd;
    const int fillFrom = std::max(0, b.beats - 1);   // the fill takes the last beat
    if (b.formStart) {
        hit(0, CRASH, b.chorus == 1 ? 90 : 100);
    }
    for (int k = 0; k < b.beats; ++k) {
        const bool inFill = fill && k >= fillFrom;
        if (g == "swing" || g == "waltz" || g == "ballad") {
            if (!(b.formStart && k == 0)) {
                hit(k, RIDE, g == "ballad" ? 55 : 75);
            }
            const bool skip = g == "waltz" ? k != 1 : (k % 2 == 1);
            if (skip && g != "ballad") {
                hit(k + 0.5, RIDE, 60);
            }
            if ((g == "waltz" && k > 0) || (g != "waltz" && k % 2 == 1)) {
                hit(k, PEDAL_HAT, 70);
            }
            hit(k, KICK, 28);   // feathered
            if (!inFill && g != "ballad" && chance(0.12)) {
                hit(k + 0.5, SNARE, 45);   // a comping snare
            }
        } else if (g == "bossa") {
            hit(k, HAT, 55);
            hit(k + 0.5, HAT, 45);
            if (k % 2 == 0) {
                hit(k, KICK, 75);
                hit(k + 1.5, KICK, 60);
            }
        } else if (g == "samba") {
            for (double s : { 0.0, 0.25, 0.5, 0.75 }) {
                hit(k + s, SHAKER, s == 0 ? 70 : 45);
            }
            hit(k, KICK, 80);
            hit(k + 0.75, KICK, 55);
        } else if (g == "latin") {
            hit(k, BELL, 70);
            hit(k + 0.5, BELL, 50);
            if (k == 1 || k == 3) {
                hit(k + 0.5, KICK, 75);
                hit(k, PEDAL_HAT, 60);
            }
        } else if (g == "funk") {
            hit(k, HAT, 70);
            hit(k + 0.5, HAT, 55);
            if (k % 2 == 1) {
                hit(k, SNARE, 100);
            } else {
                hit(k, KICK, 100);
                if (chance(0.5)) {
                    hit(k + 0.75, KICK, 80);
                }
            }
            if (!inFill && chance(0.3)) {
                hit(k + 0.25 + 0.5 * std::uniform_int_distribution<int>(0, 1)(m_rng), SNARE, 32);   // ghost notes
            }
        } else if (g == "shuffle") {
            hit(k, HAT, 70);
            hit(k + 0.5, HAT, 55);
            hit(k, k % 2 == 1 ? SNARE : KICK, 95);
        } else {   // rock
            hit(k, HAT, 70);
            hit(k + 0.5, HAT, 55);
            if (k % 2 == 1) {
                hit(k, SNARE, 100);
            } else {
                hit(k, KICK, 100);
            }
            if (k == 2 && chance(0.4)) {
                hit(k + 0.5, KICK, 85);
            }
        }
    }
    // the bossa's cross-stick and the latin clave, two bars long (3-2)
    if (g == "bossa" || g == "latin") {
        const bool first = (b.formBar % 2) == 0;
        const std::vector<double> clave = first ? std::vector<double> { 0, 1.5, 3 } : std::vector<double> { 1, 2 };
        for (double c : clave) {
            hit(c, g == "latin" ? COWBELL : SIDESTICK, 70);
        }
    }
    // a fill on the last beat of a phrase
    if (fill && b.beats >= 2) {
        const double at = fillFrom;
        if (m_swung || b.compound) {
            const int sounds[] = { SNARE, HIGH_TOM, LOW_TOM };
            for (int i = 0; i < 3; ++i) {
                hit(at + i / 3.0, sounds[i], 80 + i * 6);
            }
        } else {
            const int sounds[] = { SNARE, SNARE, MID_TOM, LOW_TOM };
            for (int i = 0; i < 4; ++i) {
                hit(at + i * 0.25, sounds[i], 78 + i * 6);
            }
        }
    }
    Q_UNUSED(OPEN_HAT);
}

void Maker::comp(const BarCtx& b, double beat, double lenBeats, int v)
{
    const Harmony h = harmonyAt(b.formBar, std::min(beat, double(b.beats) - 0.01) * b.beatQ);
    // a rootless voicing: third (or the sus note), seventh (or sixth), ninth unless it clashes, fifth or thirteenth
    std::vector<int> pcs;
    const int third = h.third();
    const int seventh = h.seventh();
    pcs.push_back(mod12(h.root + third));
    if (seventh >= 0) {
        pcs.push_back(mod12(h.root + seventh));
    }
    bool hasExt = false;
    for (size_t i = 4; i < h.tones.size(); ++i) {
        pcs.push_back(mod12(h.root + h.tones[i]));
        hasExt = true;
    }
    const bool dimish = h.fifth() == 6 && (seventh == 9 || seventh == 10);
    if (!hasExt && !dimish && third != 2) {
        pcs.push_back(mod12(h.root + 2));   // the ninth
    }
    if (pcs.size() < 4) {
        pcs.push_back(mod12(h.root + h.fifth()));
    }
    if (pcs.size() < 3) {
        pcs.push_back(h.root);
    }
    // the voicing nearest the one before: each rotation stacked upward from near the middle of the piano
    std::vector<int> best;
    double bestCost = 1e9;
    const int centre = m_lastVoicing.empty() ? 60 : m_lastVoicing.front();
    for (size_t r = 0; r < pcs.size(); ++r) {
        std::vector<int> v;
        int p = inRange(pcs[r], centre, 48, 66);
        v.push_back(p);
        for (size_t i = 1; i < pcs.size(); ++i) {
            int q = p + mod12(pcs[(r + i) % pcs.size()] - p);
            if (q == p) {
                q += 12;
            }
            v.push_back(q);
            p = q;
        }
        if (v.back() > 79) {
            continue;
        }
        double cost = 0;
        for (size_t i = 0; i < v.size() && i < m_lastVoicing.size(); ++i) {
            cost += std::abs(v[i] - m_lastVoicing[i]);
        }
        cost += std::abs(v.front() - 57) * 0.3;
        if (cost < bestCost) {
            bestCost = cost;
            best = v;
        }
    }
    if (best.empty()) {
        return;
    }
    m_lastVoicing = best;
    const double start = timeOf(b, beat);
    const double end = beat + lenBeats <= b.beats ? timeOf(b, beat + lenBeats) : timeOf(b, b.beats);
    for (int p : best) {
        note(m_piano, start, std::max(0.05, (end - start) * 0.95), p, vel(v, 5));
    }
}

void Maker::piano(const BarCtx& b, const BarCtx* next)
{
    Q_UNUSED(next);
    const QString& g = m_s.groove;
    auto changesAt = [&](double beat) {
        // a chord starting inside the bar: comp it there too
        for (const JamChord& c : b.bar->chords) {
            if (std::abs(c.at - beat * b.beatQ) < 1e-6) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::pair<double, double> > hits;   // (beat, length in beats)
    if (g == "swing" || g == "shuffle") {
        static const std::vector<std::vector<std::pair<double, double> > > rhythms {
            { { 0, 1 }, { 1.5, 0.5 } },        // Charleston
            { { 1.5, 1 } },                    // the and of two
            { { 0.5, 0.5 }, { 2, 1 } },
            { { 1, 0.5 }, { 2.5, 1 } },
            { { 0, 2 } },
            { { 2.5, 0.5 }, { 3.5, 0.5 } },
        };
        hits = rhythms[size_t(std::uniform_int_distribution<int>(0, int(rhythms.size()) - 1)(m_rng))];
    } else if (g == "ballad") {
        hits = { { 0, 2 }, { 2, 2 } };
    } else if (g == "waltz") {
        hits = { { 1, 0.9 }, { 2, 0.9 } };
    } else if (g == "bossa") {
        hits = (b.formBar % 2 == 0) ? std::vector<std::pair<double, double> > { { 0, 0.5 }, { 1.5, 0.5 }, { 3, 0.5 } }
               : std::vector<std::pair<double, double> > { { 0.5, 0.5 }, { 2, 0.5 }, { 3.5, 0.5 } };
    } else if (g == "latin" || g == "samba") {
        hits = { { 0.5, 0.4 }, { 1.5, 0.4 }, { 2.5, 0.4 }, { 3.5, 0.4 } };
    } else if (g == "funk") {
        hits = chance(0.5) ? std::vector<std::pair<double, double> > { { 0, 0.25 }, { 0.75, 0.25 }, { 2.5, 0.25 }, { 3.25, 0.25 } }
               : std::vector<std::pair<double, double> > { { 1, 0.25 }, { 1.75, 0.5 }, { 3, 0.25 } };
    } else {   // rock
        hits = { { 0, 1.5 }, { 1.5, 1 }, { 2.5, 1.5 } };
    }
    // patterns are for 4 beats: repeated for longer bars, cut short for shorter ones
    std::vector<std::pair<double, double> > all;
    for (double base = 0; base < b.beats; base += 4) {
        for (const auto& [beat, len] : hits) {
            if (base + beat < b.beats - 1e-6) {
                all.push_back({ base + beat, len });
            }
        }
    }
    // a chord that starts on a beat nobody comps on gets a hit there
    for (int k = 1; k < b.beats; ++k) {
        if (changesAt(k) && std::none_of(all.begin(), all.end(), [&](const auto& h) { return std::abs(h.first - k) < 0.6; })) {
            all.push_back({ double(k), 1.0 });
        }
    }
    std::sort(all.begin(), all.end());
    const int v = g == "ballad" ? 58 : g == "funk" ? 78 : 66;
    for (const auto& [beat, len] : all) {
        comp(b, beat, len, v);
    }
}
}

Jam makeJam(const JamForm& form, const JamSettings& settings)
{
    Maker m(form, settings);
    return m.make();
}

// ---------------------------------------------------------------------------------------------------------------------
//  Playing it
// ---------------------------------------------------------------------------------------------------------------------

JamPlayer::JamPlayer(const muse::modularity::ContextPtr& ctx, QObject* parent)
    : QObject(parent), muse::Contextable(ctx)
{
}

JamPlayer::~JamPlayer()
{
    clear();
}

void JamPlayer::clear()
{
    ++m_generation;
    m_ready = false;
    if (m_player) {
        m_player->stop();
        m_player->playbackPositionChanged().disconnect(this);
        m_player->playbackStatusChanged().disconnect(this);
        m_player.reset();
    }
    if (m_sequence >= 0 && playback()) {
        playback()->removeSequence(m_sequence);
    }
    m_sequence = -1;
    m_tracks.clear();
    if (m_playing) {
        m_playing = false;
        emit playingChanged();
    }
}

static mpe::PlaybackData jamPlaybackData(const JamTrack& track, const mpe::PlaybackSetupData& setup)
{
    mpe::PlaybackData data;
    data.setupData = setup;
    for (const JamNote& n : track.notes) {
        mpe::ArrangementContext a;
        a.nominalTimestamp = a.actualTimestamp = mpe::timestamp_t(n.start * 1000000.0);
        a.nominalDuration = a.actualDuration = std::max<mpe::duration_t>(1000, mpe::duration_t(n.length * 1000000.0));
        a.bps = 2.0;
        mpe::PitchContext p;
        p.nominalPitchLevel = mpe::pitch_level_t((n.pitch - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT) * mpe::PITCH_LEVEL_STEP);
        mpe::ExpressionContext e;
        e.nominalDynamicLevel = mpe::dynamicLevelFromType(mpe::DynamicType::mf);
        e.velocityOverride = std::clamp(n.velocity, 1, 127) / 127.0f;
        data.originEvents[a.nominalTimestamp].emplace_back(mpe::NoteEvent(std::move(a), std::move(p), std::move(e)));
    }
    return data;
}

void JamPlayer::load(const Jam& jam, bool playNow)
{
    clear();
    m_jam = jam;
    m_position = 0;
    m_playWhenReady = playNow;
    emit positionChanged();
    if (!playback() || jam.tracks.empty()) {
        return;
    }
    const int generation = m_generation;
    playback()->addSequence().onResolve(this, [this, generation](const TrackSequenceId& id) {
        if (generation != m_generation) {
            playback()->removeSequence(id);
            return;
        }
        m_sequence = id;
        m_player = playback()->player(id);
        if (!m_player) {
            return;
        }
        m_player->playbackPositionChanged().onReceive(this, [this](const secs_t& s) {
            m_position = double(s);
            emit positionChanged();
            if (m_position >= m_jam.seconds) {
                stop();
            }
        });
        m_player->playbackStatusChanged().onReceive(this, [this](PlaybackStatus st) {
            const bool playing = st == PlaybackStatus::Running;
            if (playing != m_playing) {
                m_playing = playing;
                emit playingChanged();
            }
        });
        addTracks([this, generation]() {
            if (generation != m_generation || !m_player) {
                return;
            }
            m_player->setDuration(msecs_t((m_jam.seconds + 1.0) * 1000.0));
            m_ready = true;
            emit ready();
            if (m_playWhenReady) {
                play();
            }
        });
    });
}

void JamPlayer::addTracks(const std::function<void()>& done)
{
    // each instrument's sound from the basic sound profile (MS Basic), found by its sound
    const mu::playback::SoundProfile& profile = profilesRepo()->profile(playbackConfiguration()->basicSoundProfileName());
    //! the sound with this id and these sub-categories ("acoustic", "plucked"…), else any with the id
    auto findSetup = [&](const QString& id, const QStringList& subs = {}) -> std::pair<mpe::PlaybackSetupData, AudioResourceMeta> {
        std::pair<mpe::PlaybackSetupData, AudioResourceMeta> any;
        for (const auto& [setup, meta] : profile.data) {
            if (setup.id != muse::String::fromQString(id)) {
                continue;
            }
            const bool all = std::all_of(subs.begin(), subs.end(), [&](const QString& sc) {
                return std::find(setup.subCategories.begin(), setup.subCategories.end(), muse::String::fromQString(sc))
                       != setup.subCategories.end();
            });
            if (all) {
                return { setup, meta };
            }
            if (!any.first.isValid()) {
                any = { setup, meta };
            }
        }
        return any;
    };
    auto remaining = std::make_shared<int>(int(m_jam.tracks.size()));
    const int generation = m_generation;
    for (const JamTrack& track : m_jam.tracks) {
        // (the bass: an upright, General MIDI's acoustic bass, for swing, ballads and the Latin grooves; an electric
        // one for funk and rock)
        auto [setup, meta] = track.instrument == "drums" ? findSetup("drumset")
                             : track.instrument == "bass" ? findSetup("bass_guitar", { m_jam.electricBass ? "electric" : "acoustic", "plucked" })
                             : findSetup("piano", { "grand" });
        if (!setup.isValid() && track.instrument == "bass") {
            std::tie(setup, meta) = findSetup("contrabass");
        }
        if (!setup.isValid() && track.instrument == "piano") {
            std::tie(setup, meta) = findSetup("piano");
        }
        if (!setup.isValid()) {
            LOGW() << "[starscore] jam: no sound for " << track.instrument.toStdString();
            if (--*remaining == 0) {
                done();
            }
            continue;
        }
        AudioInputParams in { meta, {} };
        AudioOutputParams out;
        out.volume = track.instrument == "drums" ? -3.f : track.instrument == "piano" ? -4.f : 0.f;
        playback()->addTrack(m_sequence, ("Jam " + track.instrument).toStdString(), jamPlaybackData(track, setup),
                             { std::move(in), std::move(out) })
        .onResolve(this, [this, remaining, done, generation](const TrackId id, const AudioParams&) {
            if (generation != m_generation) {
                return;
            }
            m_tracks.push_back(id);
            if (--*remaining == 0) {
                done();
            }
        })
        .onReject(this, [remaining, done](int code, const std::string& text) {
            LOGE() << "[starscore] jam: a track couldn't be added: " << code << " " << text;
            if (--*remaining == 0) {
                done();
            }
        });
    }
}

void JamPlayer::play()
{
    if (!m_player) {
        m_playWhenReady = true;
        return;
    }
    if (m_position >= m_jam.seconds - 0.05) {
        m_player->seek(0);
        m_position = 0;
    }
    m_player->play();
}

void JamPlayer::stop()
{
    m_playWhenReady = false;
    if (m_player) {
        m_player->stop();
        m_player->seek(0);
    }
    m_position = 0;
    emit positionChanged();
}

void JamPlayer::renderWav(const QString& path, const std::function<void(bool)>& done)
{
    if (m_sequence < 0 || !m_ready || !playback()) {
        done(false);
        return;
    }
    SoundTrackFormat format;
    format.type = SoundTrackType::WAV;
    format.outputSpec.sampleRate = 44100;
    format.outputSpec.samplesPerChannel = 1024;
    format.outputSpec.audioChannelCount = 2;
    format.sampleFormat = AudioSampleFormat::Int16;
    playback()->saveSoundTrack(m_sequence, muse::io::path_t(path), std::move(format))
    .onResolve(this, [done](const bool ok) { done(ok); })
    .onReject(this, [done](int code, const std::string& text) {
        LOGE() << "[starscore] jam: couldn't render: " << code << " " << text;
        done(false);
    });
}

const JamBarTime* JamPlayer::barAt(double seconds) const
{
    const JamBarTime* found = nullptr;
    for (const JamBarTime& b : m_jam.bars) {
        if (b.start <= seconds + 1e-6) {
            found = &b;
        } else {
            break;
        }
    }
    return found;
}
}
