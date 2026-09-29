/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <map>
#include <vector>

#include <vector>

#include <QString>
#include <QStringList>

#include "modularity/imoduleinterface.h"
#include "global/io/path.h"
#include "global/types/ret.h"
#include "global/types/retval.h"
#include "global/async/notification.h"

namespace mu::project {
class INotationProject;

//! Status shown by the coloured dot on each section (and, derived, each arrangement).
enum class StarScoreStatus {
    Empty = 0,
    Sketch,
    InProgress,
    NeedsReview,
    Finished
};

//! A section is a named group of instruments inside the one score: "Lead Sheet", "3-Horn Section",
//! "Rhythm Section". A part can (rarely) belong to more than one section. Parts in no section are
//! never touched by the toggles.
//!
//! A section is "on" when any of its instruments is visible. Turning it off remembers which of its
//! instruments were visible, so turning it back on restores them (e.g. Congas stay hidden).
struct StarScoreSection
{
    QString id;
    QString name;
    QString templateKey;     // "lead-sheet", "3-horn", "rhythm", … or "custom"
    StarScoreStatus status = StarScoreStatus::InProgress;
    bool on = true;          // derived, not stored
    QStringList partIds;     // muse::ID of each part, as text
    QStringList shownPartIds;  // parts to show when the section is turned on
    //! Rhythm sections marked Finished: players that read from the lead sheet instead of their own sheet
    //! ("drums", "percussion", "keys"); those sheets start unticked in the band export
    QStringList skipSheets;
};

//! An arrangement is a named set of sections, e.g. "3-Horn Standard" = Lead Sheet + 3-Horn Section + Rhythm Section.
//! Clicking an arrangement shows exactly its sections and hides/mutes every other section.
struct StarScoreArrangement
{
    QString id;
    QString name;
    QString templateKey;     // template it was made from, or empty
    QStringList sectionIds;
    QString scoreName;       // name of the arrangement's own score (a part book of all its instruments)
};

struct StarScoreInstrument
{
    QString instrumentId;        // MuseScore instrument id, e.g. "bb-trumpet"
    QString partName;            // name for the part, e.g. "Trumpet" (empty = MuseScore default)
    bool hidden = false;         // hidden when the section is first created (e.g. Congas)
    std::vector<int> hiddenStaves;  // staves of the instrument hidden by default (e.g. 1 = bass staff of a grand staff)
    // Playable ranges in concert pitch (MIDI numbers; -1 = keep the instrument's own). MuseScore colours notes
    // outside the amateur range dark yellow and outside the pro range red.
    int minPitchA = -1;
    int maxPitchA = -1;
    int minPitchP = -1;
    int maxPitchP = -1;
};

struct StarScoreSectionTemplate
{
    QString key;
    QString name;
    std::vector<StarScoreInstrument> instruments;
};

struct StarScoreArrangementTemplate
{
    QString key;
    QString name;
    QStringList sectionKeys;   // StarScoreSectionTemplate keys
};

struct StarScoreNewOptions
{
    QString title;
    QString composer;
    int keyFifths = 0;          // -7 … 7
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int tempoBpm = 120;
    int measures = 32;
    QString arrangementTemplateKey = "3-horn-standard";
};

//! "Always give this part book this style": a MuseScore style file (.mss) applied to part books
//! whose instrument is in a section made from `sectionKey` (empty = any section) and whose
//! part name is `partName` (empty = any part). Rules are app-wide, not per file.
struct StarScoreStyleRule
{
    QString sectionKey;
    QString partName;
    QString stylePath;
};

//! A solo transcription stored inside the .starscore as its own score (entry "StarScoreSolos/<id>.mscz").
//! It covers bars startBar..endBar of the main score, played `passes` times (1 = written out).
struct StarScoreSolo
{
    QString id;
    QString name;
    QString file;
    int startBar = 1;
    int endBar = 1;
    int passes = 1;
    int soloBars = 0;
};

//! A reference PDF (e.g. the original chart of a cover) stored inside the .starscore
//! (entry "StarScoreReferences/<file>"). Copied to "Reference PDFs/" when exporting to Sheets and Demos.
struct StarScoreReference
{
    QString id;
    QString name;                // shown in the menu and used as the exported file name, e.g. "Bet - Original Chart"
    QString file;                // entry inside the .starscore
    bool invert = true;          // shown with inverted colours in the panel (never in exports)
    QString instrument;          // which part it is for, e.g. "Bass Guitar" (empty = the whole band / none)
};

//! What adding a solo file would do, shown before the user confirms
struct StarScoreSoloPlan
{
    int soloBars = 0;
    int startBar = 1;
    int endBar = 1;
    int passes = 1;
    bool repeated = false;
    QString summary;
    QString warning;
};

//! One PDF the band export will write, relative to the song's folder in "Sheets and Demos"
//! One instrument's part in "Compare parts": per bar, 0 = same as the reference part, 1 = different, 2 = both rest, 3 = same apart from the octave
struct StarScoreComparedPart
{
    QString partId;
    QString label;               // e.g. "Trumpet — 3-Horn Section"
    QString summary;             // e.g. "Differs from 3-Horn Section in bars 17–24, 41"
    bool isReference = false;
    std::vector<int> bars;
};
struct StarScoreComparison
{
    QString instrument;          // e.g. "Trumpet"
    int barCount = 0;
    std::vector<StarScoreComparedPart> parts;
};

//! "Check voice order": one rule between two parts of a section. Per bar: 0 = in order, 1 = crossed (the lower
//! part goes above the upper), 2 = doubling (same pitch), 3 = crossed where that is allowed but not preferred,
//! 4 = the two parts never sound together in the bar
struct StarScoreVoiceRule
{
    QString upperPartId;
    QString lowerPartId;
    QString label;               // e.g. "Trumpet 1 above Alto Saxophone"
    QString summary;             // e.g. "Crossed in bars 12, 30–31; doubled in bar 7"
    bool strict = true;
    std::vector<int> bars;
};
struct StarScoreVoiceSection
{
    QString section;
    int barCount = 0;
    std::vector<StarScoreVoiceRule> rules;
};

struct StarScoreBandFile
{
    QString relativePath;        // e.g. "3H Tpt Alt Ten/AMPL - Alto Sax.pdf"
    QStringList partIds;         // one part = its part book; several = a score of just those instruments
    bool isScore = false;
    bool defaultUnchecked = false;   // left unticked in the export dialog unless the user ticks it
    QString sourceFile;          // a file copied as it is (reference PDFs) instead of printing the score

    // "Any Horns" chair versions: the chair is re-written for a transposition and clef at export time
    bool isVersion = false;
    int transposeDiatonic = 0;   // sounding relative to written, as MuseScore stores it (Bb trumpet = -1/-2)
    int transposeChromatic = 0;
    int clef = 0;                // 0 = treble, 1 = bass, 2 = alto
    QString header;              // part name printed on the sheet, e.g. "3-Horn Arr: Horn 2 in Bb"
};

struct StarScoreBandExportPlan
{
    QString bandFolder;          // .../Sheets and Demos
    QString songFolder;          // e.g. "1 Amplitudes"
    QString code;                // e.g. "AMPL"
    std::vector<StarScoreBandFile> files;
    QStringList notes;           // things that were skipped, and why

    // The song isn't in Sheets and Demos yet: the dialog asks for its folder and code first (registerBandSong)
    bool newSong = false;
    QString title;               // song title (title frame when the score's title is empty or "Untitled score")
    QString suggestedCode;       // a four-letter code no other song uses
};

//! Opening a .mscz (or any file that isn't a .starscore): the instruments, with StarScore's guess for each one's section
struct StarScoreImportPart
{
    QString partId;
    QString name;
    QString instrumentId;
    QString suggestedSection;    // section template key, empty = not in a section
    int staves = 1;
};
struct StarScoreImportPlan
{
    std::vector<StarScoreImportPart> parts;
    QStringList suggestedArrangements;   // arrangement template keys
    QString summary;                     // e.g. "Looks like 3-Horn Standard: Lead Sheet, 3-Horn Section, Rhythm Section (no congas)"
};

struct StarScorePartInfo
{
    QString partId;
    QString name;
    bool visible = true;
    QStringList sectionIds;
};

//! StarScore features for the current score. The data lives in the score itself
//! (meta tag "starscore"), so a .starscore file is a normal MuseScore zip:
//! rename it to .mscz and MuseScore Studio opens it.
class IStarScoreService : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IStarScoreService)

public:
    virtual ~IStarScoreService() = default;

    virtual bool hasScore() const = 0;
    virtual bool isStarScoreFile() const = 0;

    virtual std::vector<StarScoreSection> sections() const = 0;
    virtual std::vector<StarScoreArrangement> arrangements() const = 0;
    virtual QString activeArrangementId() const = 0;     // empty when the sections showing match no arrangement
    virtual StarScoreStatus arrangementStatus(const QString& arrangementId) const = 0;  // least-finished section
    virtual muse::async::Notification changed() const = 0;

    virtual std::vector<StarScorePartInfo> parts() const = 0;

    // --- showing / hiding (one undo step each) ---
    virtual void showArrangement(const QString& arrangementId) = 0;
    virtual void setSectionOn(const QString& sectionId, bool on) = 0;
    virtual void soloSection(const QString& sectionId) = 0;
    virtual void setAllSectionsOn(bool on) = 0;

    // --- templates ---
    virtual std::vector<StarScoreSectionTemplate> sectionTemplates() const = 0;
    virtual std::vector<StarScoreArrangementTemplate> arrangementTemplates() const = 0;

    //! Create a brand-new score holding the template arrangement (3-Horn Standard by default) and make it current.
    virtual muse::Ret newStarScore(const StarScoreNewOptions& options) = 0;

    // --- sections ---
    //! Add the template's instruments (empty, following the score's bars and structure) as a new section.
    virtual muse::RetVal<QString> createSectionFromTemplate(const QString& templateKey) = 0;
    virtual muse::RetVal<QString> createSection(const QString& templateKey, const QString& name,
                                                const std::vector<StarScoreInstrument>& instruments) = 0;
    virtual muse::RetVal<QString> createSectionFromParts(const QString& name, const QStringList& partIds) = 0;
    virtual void setSectionStatus(const QString& sectionId, StarScoreStatus status) = 0;
    //! which: "drums", "percussion" or "keys"
    virtual void setSectionSkipSheet(const QString& sectionId, const QString& which, bool skip) = 0;
    virtual void renameSection(const QString& sectionId, const QString& name) = 0;
    virtual void setSectionParts(const QString& sectionId, const QStringList& partIds) = 0;
    virtual void moveSection(const QString& sectionId, int newIndex) = 0;
    //! With deleteInstruments, also deletes the section's instruments that no other section uses.
    virtual void removeSection(const QString& sectionId, bool deleteInstruments) = 0;

    // --- arrangements ---
    //! Uses sections that already exist (matched by template key, e.g. an existing "Rhythm Section"),
    //! creates only the missing ones, then shows the new arrangement.
    virtual muse::RetVal<QString> createArrangementFromTemplate(const QString& templateKey) = 0;
    virtual muse::RetVal<QString> createArrangement(const QString& name, const QStringList& sectionIds) = 0;
    virtual void renameArrangement(const QString& arrangementId, const QString& name) = 0;
    virtual void setArrangementSections(const QString& arrangementId, const QStringList& sectionIds) = 0;
    virtual void moveArrangement(const QString& arrangementId, int newIndex) = 0;
    virtual void removeArrangement(const QString& arrangementId) = 0;

    //! Build sections from the score's existing part books ("3-Horn Arrangement", "Lead Sheet", …)
    //! and instrument types (rhythm section), plus a matching arrangement for each horn size.
    //! Returns how many sections were added.
    virtual int detectSections() = 0;

    // --- styles ---
    //! Style applied first to the main score and every part book (empty = none)
    virtual QString defaultStylePath() const = 0;
    virtual void setDefaultStylePath(const QString& path) = 0;
    virtual std::vector<StarScoreStyleRule> styleRules() const = 0;
    virtual void setStyleRules(const std::vector<StarScoreStyleRule>& rules) = 0;
    //! Apply the default style and matching rules. With partIds, only part books for those parts
    //! (and not the main score). Returns how many scores were restyled.
    virtual int applyStyles(const QStringList& partIds = {}) = 0;

    // --- solo transcriptions ---
    virtual std::vector<StarScoreSolo> solos() const = 0;
    virtual QString currentSoloId() const = 0;          // empty when the main score is showing
    virtual bool canAddSolos() const = 0;               // the main score is a saved .starscore
    virtual bool isSoloProject(const INotationProject* project) const = 0;
    virtual bool hasUnsavedSolos() const = 0;
    virtual muse::io::path_t mainProjectPath() const = 0;
    virtual muse::RetVal<StarScoreSoloPlan> planSolo(const muse::io::path_t& soloFile, int startBar, int endBar) const = 0;
    virtual muse::RetVal<QString> addSolo(const muse::io::path_t& soloFile, const QString& name, int startBar, int endBar) = 0;
    virtual muse::Ret showSolo(const QString& soloId) = 0;
    virtual muse::Ret showMainScore() = 0;
    //! Re-copy the band's music (repeats written out) into the solo view
    virtual muse::Ret refreshSoloBand(const QString& soloId) = 0;
    virtual void renameSolo(const QString& soloId, const QString& name) = 0;
    virtual void removeSolo(const QString& soloId) = 0;
    virtual muse::Ret exportSolo(const QString& soloId, const muse::io::path_t& msczPath) = 0;

    // --- reference PDFs (kept inside the .starscore; saved with it) ---
    virtual std::vector<StarScoreReference> references() const = 0;
    virtual muse::RetVal<QString> addReference(const muse::io::path_t& pdfFile) = 0;
    //! Name of a reference PDF in this score with exactly the same contents as pdfFile, or empty
    virtual QString identicalReferenceName(const muse::io::path_t& pdfFile) const = 0;
    virtual void removeReference(const QString& referenceId) = 0;
    virtual muse::io::path_t referencePath(const QString& referenceId) const = 0;   // working copy on disk
    //! The reference shown in the Reference PDF panel beside the score
    virtual QString currentReferenceId() const = 0;
    virtual void setCurrentReferenceId(const QString& referenceId) = 0;
    virtual int referencePageCount(const QString& referenceId) const = 0;          // 0 when it can't be read
    virtual void setReferenceInvert(const QString& referenceId, bool invert) = 0;
    virtual void renameReference(const QString& referenceId, const QString& name) = 0;
    virtual void setReferenceInstrument(const QString& referenceId, const QString& instrument) = 0;
    virtual void moveReference(const QString& referenceId, int newIndex) = 0;
    //! Instrument names in this score, for tagging reference PDFs
    virtual QStringList referenceInstrumentChoices() const = 0;
    //! One page (0-based) drawn at widthPx wide; the PNG's path, or empty on failure
    virtual QString referencePageImage(const QString& referenceId, int page, int widthPx) const = 0;
    //! Save changed solos and the main score (used when Save is pressed while a solo is showing)
    virtual muse::Ret saveAll() = 0;

    // --- export to the band's "Sheets and Demos" folder ---
    virtual QString bandFolder() const = 0;          // saved choice, or the usual Google Drive location if it exists
    virtual void setBandFolder(const QString& path) = 0;
    virtual muse::RetVal<StarScoreBandExportPlan> planBandExport() const = 0;
    //! Add a new song to Sheets and Demos: make its folder ("1 Title", "2 Title", "3 Title" or
    //! "4 Works In Progress/Title") with Demos and Version History inside, record its code in
    //! 6 Inbox/.organizer/codes.json, and give the score this title if it had none. category is 1–4.
    virtual muse::Ret registerBandSong(const QString& title, int category, const QString& code) = 0;

    //! "Export as MuseScore files": each arrangement as its own .mscz in folder, named "CODE - <arrangement>.mscz"
    //! (or "<title> - <arrangement>.mscz" when the song has no code). Returns a summary.
    virtual muse::RetVal<QString> exportArrangementsAsMscz(const QString& folder) = 0;
    //! Write every PDF in the plan, moving any file it replaces to "Version History/Superseded <date>/".
    //! Returns a summary.
    //! Exports the planned sheets whose relative paths are in onlyPaths (all of them when onlyPaths is empty)
    virtual muse::RetVal<QString> exportToBandFolder(const QStringList& onlyPaths) = 0;

    // --- Version number ("Version 4.0.1" in the copyright text, printed at the bottom of every page)
    virtual QString scoreVersion() const = 0;                 // from the score, or the starting version for this song
    virtual void setScoreVersion(const QString& version) = 0; // main score and every part book

    // --- Each arrangement's own score
    virtual void syncArrangementScores() = 0;                 // create / update / rename them to match the arrangements
    virtual void openArrangementScore(const QString& arrangementId) = 0;

    // --- Compare parts on the same instrument across sections
    //! referenceByInstrument: instrument name (StarScoreComparison::instrument) -> part id to compare the others with (first part when missing)
    virtual std::vector<StarScoreComparison> compareParts(const std::map<QString, QString>& referenceByInstrument) const = 0;
    virtual void selectBar(const QString& partId, int bar) = 0;   // bar is 1-based

    // --- Minor-major seventh symbol (triangle with a bar) per part: on by default for Starsign parts,
    // off for big band, orchestra and marching band parts; can be switched for the part or score being viewed
    virtual bool minMajSymbolInCurrentScore() const = 0;
    virtual void setMinMajSymbolInCurrentScore(bool on) = 0;

    // --- Check the order of the voices within each horn / wind / string section
    virtual std::vector<StarScoreVoiceSection> checkVoiceOrder() const = 0;

    //! Sheets the user un-ticked the last time this song was exported (remembered per song code)
    virtual QStringList bandExportUnticked(const QString& code) const = 0;
    virtual void setBandExportUnticked(const QString& code, const QStringList& paths) = 0;

    // --- Importing a MuseScore file: pick its sections and arrangements, optionally standardize it
    //! The current file isn't a .starscore and has no StarScore sections yet
    virtual bool needsImport() const = 0;
    virtual StarScoreImportPlan planImport() const = 0;
    //! sectionByPart: part id -> section template key ("" = none). Standardize: rename the lead to "Lead", hide its
    //! bass staff, name the other instruments as in the templates, add missing hidden instruments (e.g. Congas),
    //! add part books for every instrument, then apply the default style.
    virtual muse::Ret applyImport(const std::map<QString, QString>& sectionByPart, const QStringList& arrangementKeys,
                                  bool standardize) = 0;
    //! From now on "Save" asks for a new .starscore next to the imported file instead of overwriting it
    virtual void saveAsNewStarScore() = 0;

    // --- The StarScore panel above the score can be hidden (View › StarScore panel); remembered app-wide
    virtual bool isPanelVisible() const = 0;
    virtual void setPanelVisible(bool visible) = 0;
    virtual muse::async::Notification panelVisibleChanged() const = 0;

    //! Save a .mscz holding only the instruments and part books of this arrangement's sections.
    virtual muse::Ret exportArrangement(const QString& arrangementId, const muse::io::path_t& msczPath) = 0;
};
}
