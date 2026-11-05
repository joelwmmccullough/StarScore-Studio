/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Run in Synthesia (1.25.5, Joel 5 Nov 2026): "instead of exporting a midi file from starscore,
 * opening up synthesia, navigating through it through the terrible synthesia filesystem, re-configure everything
 * every single time, and then run it, i can instead just click a button in starscore that says 'run in synthesia'
 * and it just... works."
 *
 * One click writes the score (the one showing, or the main score: set per song) as a MIDI file without chord symbols
 * ("never send chord tracks"), with each part's sound as its MIDI program, and a Synthesia metadata file beside it
 * (Synthesia reads a .synthesia file in the same folder as the song it opens): the title, the composer, and which
 * staff is the right hand, which the left, which Synthesia plays and which it leaves out ("Parts", keyed by the MIDI
 * file's MD5). Then Synthesia opens the file. The hands and sounds are chosen once per song and kept in the .starscore.
 */
#include "starscoreservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>

#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"

#include "io/buffer.h"
#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
//! A part that's played with two hands: a keyboard instrument, or any part on two staves
bool synthesiaTwoHanded(const mu::engraving::Part* part)
{
    if (!part) {
        return false;
    }
    if (part->nstaves() >= 2) {
        return true;
    }
    const QString id = part->instrument() ? part->instrument()->id().toQString() : QString();
    return id.contains("piano") || id.contains("keyboard") || id.contains("organ") || id.contains("harpsichord")
           || id.contains("clavinet") || id.contains("celesta") || id.contains("synth");
}

QString synthesiaSafe(QString s)
{
    static const QString bad = "/\\:*?\"<>|";
    for (QChar& c : s) {
        if (bad.contains(c)) {
            c = '-';
        }
    }
    return s.simplified();
}

QString xmlAttr(QString s)
{
    s.replace('&', "&amp;");
    s.replace('<', "&lt;");
    s.replace('>', "&gt;");
    s.replace('"', "&quot;");
    return s;
}

// --- a Standard MIDI File's program changes set per track --------------------------------------------------------

bool readVlq(const QByteArray& d, int& i, quint32& out)
{
    out = 0;
    for (int n = 0; n < 4; ++n) {
        if (i >= d.size()) {
            return false;
        }
        const quint8 b = quint8(d[i++]);
        out = (out << 7) | (b & 0x7f);
        if (!(b & 0x80)) {
            return true;
        }
    }
    return false;
}

void writeVlq(QByteArray& d, quint32 v)
{
    QByteArray tmp;
    tmp.prepend(char(v & 0x7f));
    while (v >>= 7) {
        tmp.prepend(char((v & 0x7f) | 0x80));
    }
    d += tmp;
}

//! Each track's program changes set to `programs[track]` (where given; -1 leaves a track as it is), and a program
//! change put at the start of a track that has notes but none. Running status is written out in full.
QByteArray synthesiaSetPrograms(const QByteArray& smf, const std::vector<int>& programs)
{
    if (smf.size() < 14 || !smf.startsWith("MThd")) {
        return smf;
    }
    QByteArray out = smf.left(14);
    int pos = 14;
    int track = 0;
    while (pos + 8 <= smf.size()) {
        const QByteArray id = smf.mid(pos, 4);
        const quint32 len = (quint32(quint8(smf[pos + 4])) << 24) | (quint32(quint8(smf[pos + 5])) << 16)
                            | (quint32(quint8(smf[pos + 6])) << 8) | quint32(quint8(smf[pos + 7]));
        const QByteArray body = smf.mid(pos + 8, int(len));
        pos += 8 + int(len);
        if (id != "MTrk") {
            out += smf.mid(pos - 8 - int(len), 8 + int(len));
            continue;
        }
        const int want = track < int(programs.size()) ? programs[size_t(track)] : -1;
        ++track;
        if (want < 0) {
            out += "MTrk";
            out += char((len >> 24) & 0xff);
            out += char((len >> 16) & 0xff);
            out += char((len >> 8) & 0xff);
            out += char(len & 0xff);
            out += body;
            continue;
        }
        QByteArray events;
        int i = 0;
        quint8 status = 0;
        bool hadProgram = false;
        int firstNoteChannel = -1;
        bool ok = true;
        while (i < body.size()) {
            quint32 delta = 0;
            if (!readVlq(body, i, delta) || i >= body.size()) {
                ok = false;
                break;
            }
            quint8 b = quint8(body[i]);
            if (b & 0x80) {
                status = b;
                ++i;
            }
            QByteArray ev;
            if (status == 0xff) {
                if (i >= body.size()) {
                    ok = false;
                    break;
                }
                const quint8 type = quint8(body[i++]);
                quint32 mlen = 0;
                const int lenStart = i;
                if (!readVlq(body, i, mlen) || i + int(mlen) > body.size()) {
                    ok = false;
                    break;
                }
                ev += char(0xff);
                ev += char(type);
                ev += body.mid(lenStart, i - lenStart);
                ev += body.mid(i, int(mlen));
                i += int(mlen);
            } else if (status == 0xf0 || status == 0xf7) {
                quint32 slen = 0;
                const int lenStart = i;
                if (!readVlq(body, i, slen) || i + int(slen) > body.size()) {
                    ok = false;
                    break;
                }
                ev += char(status);
                ev += body.mid(lenStart, i - lenStart);
                ev += body.mid(i, int(slen));
                i += int(slen);
            } else if (status >= 0x80) {
                const quint8 kind = status & 0xf0;
                const int n = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
                if (i + n > body.size()) {
                    ok = false;
                    break;
                }
                ev += char(status);
                if (kind == 0xc0) {
                    ev += char(want & 0x7f);
                    hadProgram = true;
                } else {
                    ev += body.mid(i, n);
                }
                if (kind == 0x90 && firstNoteChannel < 0) {
                    firstNoteChannel = status & 0x0f;
                }
                i += n;
            } else {
                ok = false;
                break;
            }
            writeVlq(events, delta);
            events += ev;
        }
        if (!ok) {
            return smf;   // (not understood: sent as it was)
        }
        if (!hadProgram && firstNoteChannel >= 0) {
            QByteArray pc;
            writeVlq(pc, 0);
            pc += char(0xc0 | firstNoteChannel);
            pc += char(want & 0x7f);
            events.prepend(pc);
        }
        const quint32 n = quint32(events.size());
        out += "MTrk";
        out += char((n >> 24) & 0xff);
        out += char((n >> 16) & 0xff);
        out += char((n >> 8) & 0xff);
        out += char(n & 0xff);
        out += events;
    }
    return out;
}
}

StarScoreSynthesiaSetup StarScoreService::synthesiaSetup() const
{
    const Data data = load();
    return synthesiaSetup(data.synthesia.isEmpty() ? true : data.synthesia.value("sendShowing").toBool(true));
}

StarScoreSynthesiaSetup StarScoreService::synthesiaSetup(bool sendShowing) const
{
    using namespace mu::engraving;
    StarScoreSynthesiaSetup setup;
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    INotationPtr notation = sendShowing ? globalContext()->currentNotation() : (master ? master->notation() : nullptr);
    Score* score = notation ? notation->elements()->msScore() : nullptr;
    if (!master || !score) {
        setup.problem = muse::qtrc("starscore", "Open a score first.");
        return setup;
    }
    const Data data = load();
    setup.configured = !data.synthesia.isEmpty();
    setup.sendShowing = sendShowing;
    setup.song = master->masterScore()->metaTag(u"workTitle").toQString();
    if (setup.song.isEmpty()) {
        INotationProjectPtr project = globalContext()->currentProject();
        setup.song = project ? project->displayName() : QString();
    }
    setup.scoreName = notation == master->notation() ? muse::qtrc("starscore", "Main score")
                      : notation->name();
    const QJsonObject saved = data.synthesia.value("tracks").toObject();

    // the hands by default: the first two-handed part's upper staff the right hand and lower staff the left; a score of
    // one single-staff part, the right hand; everything else played by Synthesia
    const Part* handsPart = nullptr;
    for (const Part* p : score->parts()) {
        if (p->show() && synthesiaTwoHanded(p)) {
            handsPart = p;
            break;
        }
    }
    const bool lone = score->nstaves() == 1;
    for (staff_idx_t si = 0; si < score->nstaves(); ++si) {
        const Staff* st = score->staff(si);
        const Part* p = st ? st->part() : nullptr;
        if (!p) {
            continue;
        }
        StarScoreSynthesiaTrack t;
        const QString partName = p->partName().toQString();
        const staff_idx_t r = st->rstaff();
        t.key = partName + "#" + QString::number(r);
        t.name = p->nstaves() < 2 ? partName
                 : p->nstaves() == 2 ? (r == 0 ? muse::qtrc("starscore", "%1 (upper staff)").arg(partName)
                                       : muse::qtrc("starscore", "%1 (lower staff)").arg(partName))
                 : muse::qtrc("starscore", "%1 (staff %2)").arg(partName).arg(r + 1);
        t.firstOfPart = r == 0;
        t.hand = lone ? "R" : p == handsPart ? (r == 0 ? "R" : r == 1 ? "L" : "B") : (p->show() ? "B" : "X");
        const Instrument* inst = p->instrument();
        const InstrChannel* ch = inst && !inst->channel().empty() ? inst->channel(0) : nullptr;
        t.program = ch && ch->program() >= 0 ? ch->program() : 0;
        const QJsonObject mine = saved.value(t.key).toObject();
        if (!mine.isEmpty()) {
            t.hand = mine.value("hand").toString(t.hand);
            t.program = mine.value("program").toInt(t.program);
        }
        // (one sound for a part: its staves share a MIDI channel)
        const QJsonObject first = saved.value(partName + "#0").toObject();
        if (!first.isEmpty()) {
            t.program = first.value("program").toInt(t.program);
        }
        setup.tracks.push_back(t);
    }
    return setup;
}

void StarScoreService::setSynthesiaSetup(const StarScoreSynthesiaSetup& setup)
{
    mu::engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }
    Data data = load();
    QJsonObject tracks = data.synthesia.value("tracks").toObject();
    for (const StarScoreSynthesiaTrack& t : setup.tracks) {
        tracks[t.key] = QJsonObject { { "hand", t.hand }, { "program", t.program } };
    }
    data.synthesia = QJsonObject { { "sendShowing", setup.sendShowing }, { "tracks", tracks } };
    storeTo(ms, data, projectOf(ms));
}

QString StarScoreService::runInSynthesia()
{
    const StarScoreSynthesiaSetup setup = synthesiaSetup();
    if (!setup.problem.isEmpty()) {
        return setup.problem;
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    INotationPtr notation = setup.sendShowing ? globalContext()->currentNotation() : master->notation();
    INotationWriterPtr writer = writers()->writer("mid");
    if (!notation || !writer) {
        return muse::qtrc("starscore", "The score couldn't be written as MIDI.");
    }

    // the MIDI file, without the chord symbols' playback
    muse::io::Buffer buf;
    buf.open(muse::io::IODevice::WriteOnly);
    INotationWriter::Options options { { INotationWriter::OptionKey::STARSCORE_NO_CHORD_SYMBOLS, Val(true) } };
    if (!writer->write(notation, buf, options)) {
        return muse::qtrc("starscore", "The score couldn't be written as MIDI.");
    }
    std::vector<int> programs;
    QStringList parts;
    for (size_t i = 0; i < setup.tracks.size(); ++i) {
        programs.push_back(setup.tracks[i].program);
        parts << QString("t%1:%2A").arg(i).arg(setup.tracks[i].hand.isEmpty() ? QString("B") : setup.tracks[i].hand);
    }
    const QByteArray midi = synthesiaSetPrograms(buf.data().toQByteArray(), programs);

    // where Synthesia finds it: a folder of StarScore's own, the .synthesia file beside the song
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Synthesia";
    QDir().mkpath(folder);
    QString base = synthesiaSafe(setup.song.isEmpty() ? QString("Score") : setup.song);
    if (notation != master->notation()) {
        base += " - " + synthesiaSafe(setup.scoreName);
    }
    const QString midPath = folder + "/" + base + ".mid";
    const QString metaPath = folder + "/" + base + ".synthesia";
    QFile mf(midPath);
    if (!mf.open(QIODevice::WriteOnly) || mf.write(midi) != midi.size()) {
        return muse::qtrc("starscore", "The MIDI file couldn't be written to %1.").arg(folder);
    }
    mf.close();

    const QString md5 = QString::fromLatin1(QCryptographicHash::hash(midi, QCryptographicHash::Md5).toHex());
    mu::engraving::MasterScore* ms = master->masterScore();
    const QString composer = ms->metaTag(u"composer").toQString().replace('\n', " / ");
    const QString subtitle = notation != master->notation() ? setup.scoreName : QString();
    const QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                                "<SynthesiaMetadata Version=\"1\">\n"
                                "  <Songs>\n"
                                "    <Song Version=\"1\" UniqueId=\"%1\" Title=\"%2\"%3%4 Parts=\"%5\" />\n"
                                "  </Songs>\n"
                                "</SynthesiaMetadata>\n")
                        .arg(md5, xmlAttr(setup.song),
                             subtitle.isEmpty() ? QString() : QString(" Subtitle=\"%1\"").arg(xmlAttr(subtitle)),
                             composer.isEmpty() ? QString() : QString(" Composer=\"%1\"").arg(xmlAttr(composer)),
                             parts.join(' '));
    QFile xf(metaPath);
    if (!xf.open(QIODevice::WriteOnly)) {
        return muse::qtrc("starscore", "The Synthesia file couldn't be written to %1.").arg(folder);
    }
    xf.write(xml.toUtf8());
    xf.close();
    LOGI() << "[starscore] Synthesia: " << midPath << " (" << md5 << ")";

    if (qEnvironmentVariableIsSet("STARSCORE_AUTOTEST")) {
        return QString();   // (the autotest checks the files; nothing to open)
    }
#ifdef Q_OS_MAC
    if (QProcess::startDetached("/usr/bin/open", { "-a", "Synthesia", midPath })) {
        return QString();
    }
#endif
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(midPath))) {
        return muse::qtrc("starscore", "Synthesia couldn't be opened. The files are in %1.").arg(folder);
    }
    return QString();
}
