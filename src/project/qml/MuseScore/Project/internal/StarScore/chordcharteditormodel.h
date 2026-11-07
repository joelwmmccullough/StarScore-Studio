/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the chord chart editor (1.30.0, Joel 7 Nov 2026: "Please build an ireal-adjacent interface into
 * starscore so I can directly edit and adjust the chord charts that starscore exports").
 *
 * The song's iReal Pro chart as its cells, 16 to a line, as iReal Pro lays it out: each cell a chord (or a repeat-bar
 * sign, a slash, N.C., or nothing), and what iReal Pro writes around it (the bar line before and after, the section
 * box, time signature, ending, texts above and below, segno, coda, fermata, small chords). Every change is kept in the
 * song at once (IStarScoreService::setIRealEdit); the export then writes this chart instead of making one.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "project/istarscoreservice.h"

#include <functional>
#include <vector>

namespace mu::project {
//! One cell of an iReal Pro chart
struct IRealCell {
    QString content;     // a chord in iReal Pro's notation ("Eb-7", "Bb7sus", "F/A"), "x" "r" "n" "p", or "" (empty)
    QString alt;         // an alternate chord, written small above ("Ab7")
    bool small = false;
    QString left;        // "", "|", "[", "{"
    QString right;       // "", "]", "}", "Z"
    QString mark;        // "", "A".."D", "V", "i"
    QString ts;          // "", "T44", "T74", "T12" (12/8)
    int ending = 0;      // 0, 1, 2, 3
    QStringList texts;   // iReal Pro's texts as written ("*74Solos", "4x", "161 bpm")
    QString symbols;     // "S" segno, "Q" coda, "f" fermata, "Y" space above the line

    bool operator==(const IRealCell& o) const
    {
        return content == o.content && alt == o.alt && small == o.small && left == o.left && right == o.right && mark == o.mark
               && ts == o.ts && ending == o.ending && texts == o.texts && symbols == o.symbols;
    }
};

//! iReal Pro's notation <-> cells, and chords as typed <-> iReal Pro's
namespace ireal {
std::vector<IRealCell> parse(const QString& music);
QStringList serialize(const std::vector<IRealCell>& cells);   // lines of 16 cells
//! "Ebm7" "Fm7b5" "BbΔ7" "Eb7/Bb" "N.C." -> "Eb-7" "Fh7" "Bb^7" "Eb7/Bb" "n"; "" when it isn't a chord
QString chordFromTyped(const QString& typed);
//! how it reads: root, the rest raised, bass ("E♭" "m7" "" / "F" "ø7" "" / "E♭" "7" "B♭")
QStringList chordDisplay(const QString& ireal);
//! iReal Pro knows this chord's quality
bool knownQuality(const QString& ireal);
}

class ChordChartEditorModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool possible READ possible NOTIFY loaded)
    Q_PROPERTY(QString why READ why NOTIFY loaded)
    Q_PROPERTY(QString title READ title NOTIFY loaded)
    Q_PROPERTY(QString keyName READ keyName NOTIFY loaded)
    Q_PROPERTY(QString style READ style NOTIFY loaded)
    //! the chart is Joel's (kept in the song), not the one made from the chords
    Q_PROPERTY(bool edited READ edited NOTIFY cellsChanged)
    //! the chords in the score changed since this chart was edited
    Q_PROPERTY(bool stale READ stale NOTIFY cellsChanged)
    Q_PROPERTY(QVariantList cells READ cells NOTIFY cellsChanged)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY cellsChanged)
    Q_PROPERTY(int selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY selectedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY cellsChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY cellsChanged)
    Q_PROPERTY(int parts READ parts NOTIFY cellsChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit ChordChartEditorModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void select(int index);
    //! the chord as typed; false (message says why) when it isn't one
    Q_INVOKABLE bool setChord(int index, const QString& typed);
    //! "x" (repeat the bar), "r" (repeat two bars), "p" (slash), "n" (N.C.), "" (empty)
    Q_INVOKABLE void setContent(int index, const QString& content);
    Q_INVOKABLE void setSmall(int index, bool on);
    Q_INVOKABLE void setLeft(int index, const QString& barline);
    Q_INVOKABLE void setRight(int index, const QString& barline);
    Q_INVOKABLE void setMark(int index, const QString& mark);
    Q_INVOKABLE void setTimeSig(int index, const QString& code);
    Q_INVOKABLE void setEnding(int index, int ending);
    Q_INVOKABLE void setRepeatCount(int index, int times);
    Q_INVOKABLE void setTextAbove(int index, const QString& text);
    Q_INVOKABLE void setTextBelow(int index, const QString& text);
    Q_INVOKABLE void setSymbol(int index, const QString& symbol, bool on);
    Q_INVOKABLE void setAlt(int index, const QString& typed);
    //! cells in or out at index (the cells after move along); whole lines
    Q_INVOKABLE void insertCells(int index, int count);
    Q_INVOKABLE void deleteCells(int index, int count);
    Q_INVOKABLE void insertLine(int row);
    Q_INVOKABLE void deleteLine(int row);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    //! back to the chart made from the chords (Joel's version no longer kept)
    Q_INVOKABLE void startAgain();
    //! the chart in iReal Pro: its page (one link per part) opened
    Q_INVOKABLE void openInIReal();
    //! a typed chord as iReal Pro gets it and as it reads, for the field's preview ({ireal, display, known})
    Q_INVOKABLE QVariantMap preview(const QString& typed) const;
    //! the window opens again at this size
    Q_INVOKABLE void saveWindowSize(int width, int height);

    bool possible() const { return m_possible; }
    QString why() const { return m_why; }
    QString title() const { return m_title; }
    QString keyName() const { return m_key; }
    QString style() const { return m_style; }
    bool edited() const { return m_edited; }
    bool stale() const { return m_stale; }
    QVariantList cells() const;
    int rowCount() const { return int(m_cells.size()) / 16; }
    int selected() const { return m_selected; }
    QVariantMap current() const;
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    int parts() const;
    QString message() const { return m_message; }

signals:
    void loaded();
    void cellsChanged();
    void selectedChanged();
    void messageChanged();

private:
    QVariantMap cellMap(int index) const;
    IRealCell* cellAt(int index);
    void change(const std::function<void(std::vector<IRealCell>&)>& f);
    void pad();
    void store();
    void setMessage(const QString& m);

    bool m_possible = false;
    QString m_why, m_title, m_key, m_style;
    QStringList m_generated;
    bool m_edited = false;
    bool m_stale = false;
    std::vector<IRealCell> m_cells;
    int m_selected = 0;
    std::vector<std::vector<IRealCell> > m_undo, m_redo;
    QString m_message;
};
}
