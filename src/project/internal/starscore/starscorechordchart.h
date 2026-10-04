/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — chord charts from a song's lead sheet: a one-page PDF for a phone held upright and iReal Pro
 * charts (irealbook:// links in an .html file). Ported from the Python prototype (extract.py, chart.py, pdfchart.py,
 * render.py, October 2026); the analysis and the HTML are kept identical to it so the two can be diffed.
 *
 * The steps, in order:
 *   1. extractChordChart   the lead sheet's form and chords straight from the engraving model (extract.py's JSON)
 *   2. analyse             written repeats played out, sections at the rehearsal marks, each section compressed
 *   3. chordChartHtml      the PDF page as HTML (printed by the organizer's WebKit printer on macOS)
 *      chordChartIRealHtml the iReal Pro links, split into parts when the song is too long for one chart
 *   4. writeChordCharts    both files into "<song folder>/Chord Charts/", replaced files archived to Version History
 */
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mu::engraving {
class MasterScore;
}

namespace mu::project::starscore {
//! One chord symbol: "root" and "bass" as note names ("Eb", "F#"; empty = none), "q" MuseScore's typed quality
//! ("m7", "^7", "7b9", "" for a major triad), "t" its position in the bar as a fraction of a whole note ("0", "1/2")
struct ChordChartChord {
    QString root;
    QString bass;
    QString q;
    bool hidden = false;   // not shown on the lead sheet (a repeat of the chord for playback)
    QString t;
};
//! A text on the lead sheet: k = "SystemText", "StaffText" or "Tempo"; lead = on a lead staff; eid = the element's
//! id in the main score (the "Visible on chord chart" box in the inspector keeps a list of hidden ids)
struct ChordChartText {
    QString k;
    QString t;
    bool lead = false;
    QString eid;
};
struct ChordChartVolta {
    std::vector<int> endings;   // the endings this volta is played on (1, 2, …)
    int measures = 1;           // how many bars it covers
    QString text;               // its label ("1.")
};
struct ChordChartJump {
    QString to, until, cont, text;
};
struct ChordChartMarker {
    QString label, text;
};
struct ChordChartMeasure {
    int i = 0;                           // 0-based bar index
    QString ts;                          // time signature "4/4"
    QString len;                         // the bar's real length as a fraction of a whole note ("1", "1/4" for a pickup)
    std::optional<QString> rm;           // rehearsal mark
    bool startRepeat = false;
    int endRepeat = 0;                   // repeat count (0 = no end repeat)
    std::optional<ChordChartVolta> volta;
    std::vector<ChordChartJump> jumps;
    std::vector<ChordChartMarker> markers;
    std::vector<ChordChartText> texts;
    std::optional<QString> bar;          // the end barline when not plain ("double", "end")
    std::vector<ChordChartChord> chords; // one per position, from the lead staves
    //! extract.py's flag for a <Measure> carrying <multiMeasureRest>: the file's copy of a multimeasure rest, which the
    //! prototype counted as a bar of its own. The engraving model keeps those apart from the real bars, so this is
    //! always false here (the prototype's charts had one extra bar after each multimeasure rest).
    bool mmrTag = false;
};
//! extract.py's JSON object for one song
struct ChordChartData {
    QString file;
    QString title, subtitle, composer;
    QString lead;                        // the part the chords come from ("Lead")
    std::vector<int> leadStaves;         // its staves, 1-based
    std::optional<QString> leadStatus;   // the Lead Sheet section's status key ("finished", "auto", …) as stored
    std::optional<int> key;              // concert key signature (flats negative)
    QStringList chordChartHidden;        // element ids of texts left off the chart
    std::vector<ChordChartMeasure> measures;

    QJsonObject toJson() const;
};

//! The lead sheet's form and chords, from the score as it is now
ChordChartData extractChordChart(const mu::engraving::MasterScore* ms);

//! Whether a chart can be made: a part called "Lead" or "Lead Sheet" with chord symbols on it. `why` says what's
//! missing otherwise (the export's note).
bool chordChartPossible(const ChordChartData& data, QString* why = nullptr);

//! The chart title the prototype used: the file name without its code ("BALK - Balkan Wedding" -> "Balkan Wedding")
QString chordChartTitleFromFileName(const QString& path);

//! URLs for the page's three fonts (file:// for the test, data: in the app)
struct ChordChartFonts {
    QString modernoir;   // titles
    QString jost;        // chords (StarScore Jost)
    QString bravura;     // accidentals, repeat sign, metronome notes (Bravura Text)
};
//! The fonts as data: URLs, for a page printed from memory (TT Modernoir from the installed fonts when found)
ChordChartFonts chordChartEmbeddedFonts();

//! The PDF page as HTML. version: printed in a footer under the last row ("Version 4.0.1 · made by StarScore
//! Studio"); empty for none (pdfchart.py's exact output).
QString chordChartHtml(const ChordChartData& data, const QString& title, const ChordChartFonts& fonts, const QString& version);
//! The iReal Pro page: one link per part, the "written differently" notes, and the version footer (empty for none)
QString chordChartIRealHtml(const ChordChartData& data, const QString& title, const QString& version);

//! What writeChordCharts did
struct ChordChartResult {
    QStringList written;     // paths relative to the song folder ("Chord Charts/BALK - Chord Chart.pdf")
    QStringList unchanged;   // the same as the file already there
};
//! Writes "<folder>/Chord Charts/<code> - iReal Pro.html" and "<folder>/Chord Charts/<code> - Chord Chart.pdf" (the
//! PDF printed by the organizer's WebKit printer; on a system without it, the page is written as
//! "<code> - Chord Chart.html" instead and problems gets one note). A file is only replaced when it differs: the
//! html by its bytes, the PDF by what it draws (samePdf, the export's comparison); supersede(relativePath,
//! archivedTo) is the export's helper that moves the file already there to Version History.
ChordChartResult writeChordCharts(const mu::engraving::MasterScore* ms, const QString& folder, const QString& code,
                                  const QString& title, const QString& version,
                                  const std::function<bool(const QString& relativePath, QString* archivedTo)>& supersede,
                                  const std::function<bool(const QByteArray& fresh, const QString& existingPath)>& samePdf,
                                  QStringList& problems);
}
