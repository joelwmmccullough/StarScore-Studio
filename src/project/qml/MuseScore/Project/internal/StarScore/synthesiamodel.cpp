/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "synthesiamodel.h"

#include <QVariantMap>

using namespace mu::project;

SynthesiaModel::SynthesiaModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    if (starScore()) {
        m_setup = starScore()->synthesiaSetup();
    }
}

QVariantList SynthesiaModel::tracks() const
{
    QVariantList out;
    for (const StarScoreSynthesiaTrack& t : m_setup.tracks) {
        out << QVariantMap { { "name", t.name }, { "hand", t.hand }, { "program", t.program }, { "firstOfPart", t.firstOfPart } };
    }
    return out;
}

void SynthesiaModel::choose(bool sendShowing)
{
    if (starScore()) {
        const bool configured = m_setup.configured;
        m_setup = starScore()->synthesiaSetup(sendShowing);
        m_setup.configured = configured;
        emit changed();
    }
}

void SynthesiaModel::setHand(int track, const QString& hand)
{
    if (track >= 0 && track < int(m_setup.tracks.size())) {
        m_setup.tracks[size_t(track)].hand = hand;
        emit changed();
    }
}

void SynthesiaModel::setSound(int track, int program)
{
    if (track < 0 || track >= int(m_setup.tracks.size())) {
        return;
    }
    // (one sound for every staff of the part)
    const QString part = m_setup.tracks[size_t(track)].key.section('#', 0, -2);
    for (StarScoreSynthesiaTrack& t : m_setup.tracks) {
        if (t.key.section('#', 0, -2) == part) {
            t.program = program;
        }
    }
    emit changed();
}

QString SynthesiaModel::apply(bool run)
{
    if (!starScore() || !m_setup.problem.isEmpty()) {
        return m_setup.problem;
    }
    starScore()->setSynthesiaSetup(m_setup);
    return run ? starScore()->runInSynthesia() : QString();
}

QStringList SynthesiaModel::soundNames() const
{
    // General MIDI's 128 sounds, numbered as Synthesia and most players show them
    static const char* NAMES[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1",
        "Electric Piano 2", "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
        "Xylophone", "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
        "Reed Organ", "Accordion", "Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
        "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar",
        "Distortion Guitar", "Guitar Harmonics", "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
        "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello",
        "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani", "String Ensemble 1",
        "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Choir",
        "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
        "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon",
        "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)",
        "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)",
        "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
        "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)", "FX 6 (goblins)",
        "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause",
        "Gunshot"
    };
    QStringList out;
    for (int i = 0; i < 128; ++i) {
        out << QString("%1 %2").arg(i + 1).arg(QString::fromLatin1(NAMES[i]));
    }
    return out;
}
