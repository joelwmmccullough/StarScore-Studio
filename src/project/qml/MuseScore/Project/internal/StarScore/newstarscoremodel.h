/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"
#include "context/iglobalcontext.h"

namespace mu::project {
//! Behind the New StarScore dialog: the dropdown lists, the remembered choices, and creating the score
class NewStarScoreModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList arrangementTemplates READ arrangementTemplates CONSTANT)
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    //! The arrangement, doubler instrument and 7th horn chosen last time (settings), to start from
    Q_PROPERTY(QString lastArrangementKey READ lastArrangementKey CONSTANT)
    Q_PROPERTY(QString lastDoublerId READ lastDoublerId CONSTANT)
    Q_PROPERTY(QString lastLowHornId READ lastLowHornId CONSTANT)
    //! Solo Transcription (1.24.0): the Starsign songs a solo can be tied to ("Not tied" first), the instruments, and the
    //! instrument picked last time
    Q_PROPERTY(QVariantList soloInstruments READ soloInstruments CONSTANT)
    Q_PROPERTY(QString lastSoloInstrumentId READ lastSoloInstrumentId CONSTANT)
    //! The open score's title and song code (to start the form with when making a solo from an old one, or for a song)
    Q_PROPERTY(QString openTitle READ openTitle CONSTANT)
    Q_PROPERTY(QString openSongCode READ openSongCode CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit NewStarScoreModel(QObject* parent = nullptr);

    QVariantList arrangementTemplates() const;
    QVariantList keys() const;
    QString lastArrangementKey() const;
    QString lastDoublerId() const;
    QString lastLowHornId() const;
    //! The songs a solo can be tied to ("Not tied" first), with `code` in the list even when its song isn't found
    Q_INVOKABLE QVariantList songsForTying(const QString& code = QString()) const;
    QVariantList soloInstruments() const;
    QString lastSoloInstrumentId() const;
    QString openTitle() const;
    QString openSongCode() const;

    //! Standard arrangements of 3 or more horns have a woodwind doubler whose instrument is chosen
    Q_INVOKABLE bool hasDoubler(const QString& arrangementKey) const;
    //! 7-Horn Standard: the 7th horn is chosen
    Q_INVOKABLE bool hasLowHorn(const QString& arrangementKey) const;
    //! The doubler dropdown for an arrangement: "<Name> on Alto Sax (recommended)" … (the name from the band roster)
    //! matchSong (adding an arrangement to a song): the doubler instrument the song already uses is the recommended one
    Q_INVOKABLE QVariantList doublerChoices(const QString& arrangementKey, bool matchSong = false) const;
    //! The arrangement's usual doubler instrument: alto sax for 3/4/5-Horn, soprano sax for 6/7-Horn
    Q_INVOKABLE QString defaultDoublerId(const QString& arrangementKey, bool matchSong = false) const;
    //! The 7th horn dropdown: Bass Trombone (recommended), then the other low horns
    //! The 7th horn's choices; with the doubler on Bass Clarinet (Ben), no Bass Clarinet: two in the band makes no sense
    Q_INVOKABLE QVariantList lowHornChoices(const QString& doublerInstrumentId = QString()) const;

    Q_INVOKABLE bool create(const QVariantMap& options);
    //! A new Solo Transcription (convert: made from the open score, an old solo kept in a song)
    Q_INVOKABLE bool createSolo(const QVariantMap& options, bool convert = false);
    //! The copyright line a solo gets unless another is typed (from its song, player, band and recording)
    Q_INVOKABLE QString autoCopyright(const QVariantMap& options) const;

    // --- Add arrangement (the StarScore bar's menu, in an open score): the same doubler / 7th horn questions ---
    //! The arrangement template's name ("3-Horn Standard")
    Q_INVOKABLE QString arrangementName(const QString& arrangementKey) const;
    //! Whether adding this arrangement to the open score asks anything: a Standard arrangement of 3 or more horns
    //! whose horn section the score doesn't have yet
    Q_INVOKABLE bool asksOnAdd(const QString& arrangementKey) const;
    //! Adds the arrangement to the open score with the chosen doubler and 7th horn (ignored where they don't apply)
    Q_INVOKABLE bool addArrangement(const QString& arrangementKey, const QString& doublerInstrumentId,
                                    const QString& lowHornInstrumentId);
};
}
