/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice (Joel, 22 Oct 2026)
 *
 * Each song's practice library lives in its .starscore, in the main score's metaTag "starscorePractice" (base64 of
 * qCompress'd JSON), so it travels with the song and survives saving in any StarScore version. A library holds the
 * song's sections and solos and its practice items: what to play, for how long, at what tempos, when it's done, the
 * exercise as MusicXML (piano grand staff, with bass and drum backing where it has one), and an iReal Pro chart.
 *
 * From a setlist the planner makes a routine (one session of N minutes) or a plan (dated days up to a show, each
 * with its own minutes), weighting each song by what it needs, spacing items, walking each song up its items'
 * prerequisites, and ending with run-throughs. Progress (items done, tempos reached, how solid they are) is kept per
 * user and later plan days are rebuilt from it.
 */
#pragma once

#include <map>
#include <vector>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace mu::project::practice {
inline constexpr const char PRACTICE_META_TAG[] = "starscorePractice";

//! One of an item's separate exercises, with its own share of the minutes (and its own timer, example, and backing)
struct Part {
    QString label;
    int minutes = 0;
    int barFrom = 0;       // the exercise's bars (1-based) it covers; 0: the whole exercise, or none (text only)
    int barTo = 0;
};

struct Item {
    QString id;
    QString title;
    QString category;      // voicings, comping, parts, solo, vocabulary, form, technique, ear, performance (old: harmony)
    QString solo;          // the solo it belongs to, if any
    QStringList sections;
    int level = 1;         // 1-5
    int minutes = 10;
    QStringList prereq;
    bool prereqSolid = false;   // the prerequisites must be solid (stage 3), not just started
    QStringList skills;    // the skills it works on ("scale.altered", "shape.thirds", ...; see skillName)
    QString goal;
    QStringList steps;
    int tempoStart = 0;
    int tempoGoal = 0;
    QString moveOnWhen;
    QString listen;
    QStringList refs;      // "Keys part, bars 17-24"
    QByteArray xmlPacked;  // MusicXML with backing (qCompress'd UTF-8: it is most of a library's size), empty for text-only items
    bool hasXml() const { return !xmlPacked.isEmpty(); }
    QString xml() const;
    QString irealStyle, irealKey, irealProg;
    QString groove;
    std::vector<Part> parts;   // empty: one part, the whole item
};

struct Solo {
    QString id, name, bars, form, changes, soloist, notes;
};

struct Section {
    QString id, name, bars, summary;
};

struct Library {
    QString code;
    QString title;
    QString path;          // the .starscore it came from
    QString summary;
    QString style;
    QStringList instruments;
    std::map<QString, int> needs;   // parts, comping, solos: 0-3
    std::vector<Section> sections;
    std::vector<Solo> solos;
    std::vector<Item> items;
    bool valid() const { return !items.empty(); }
    const Item* item(const QString& id) const;
    const Solo* solo(const QString& id) const;
};

//! The library stored in a .starscore (an empty one when it has none or can't be read). Without `withXml` the items
//! have no notation (for indexing).
Library readLibrary(const QString& starscorePath, bool withXml = true);
//! From the metaTag's value
Library decodeLibrary(const QByteArray& blob, bool withXml = true);
Library parseLibrary(const QJsonObject& root, bool withXml = true);

//! The library of a .starscore, read once and kept while the file is unchanged
const Library& cachedLibrary(const QString& starscorePath);

//! The library index of a folder of songs: code, title, path, item count, needs (cached by file date and size)
struct SongEntry {
    QString code, title, path;
    int items = 0;
    int solos = 0;
    int minutes = 0;
    std::map<QString, int> needs;
};
std::vector<SongEntry> scanLibraries(const QString& rootFolder, const QString& cacheFile);

// ---------------------------------------------------------------- progress
//! How solid an item is: 0 new, 1 started, 2 getting there, 3 solid (move on), 4 mastered (review only)
struct ItemProgress {
    int stage = 0;
    int sessions = 0;
    int minutes = 0;
    int bestTempo = 0;
    QDate last;
    QString note;
    int level = 0;         // the item's level and skills when it was last done (0, empty: not known)
    QStringList skills;
};

class Progress
{
public:
    explicit Progress(const QString& file = QString());
    void load();
    bool save() const;
    ItemProgress get(const QString& code, const QString& itemId) const;
    void record(const QString& code, const QString& itemId, int minutes, int tempo, int stage, const QString& note,
                const QDate& date = QDate::currentDate(), int level = 0, const QStringList& skills = {});
    QJsonArray log() const { return m_log; }
    QString file() const { return m_file; }

private:
    QString m_file;
    std::map<QString, ItemProgress> m_items;   // "CODE/ID"
    QJsonArray m_log;
};

// ---------------------------------------------------------------- skills
//! How good he is at each skill, from the items done: an item of level L counts L-1, plus 0.25 started, 0.5 getting
//! there, 0.85 solid, 1 mastered; a skill's rating is its best item (0: not started; 1.85: solid at level 2, ...)
struct SkillRating {
    QString id;
    double rating = 0;
    int items = 0;         // items of the skill done at least once
    int solidItems = 0;    // ... and solid or mastered
    int sessions = 0;
    int minutes = 0;
    QDate last;
};
//! "CODE/ITEM" → the item's skills and level, for progress kept before items had skills
using SkillLookup = std::map<QString, std::pair<QStringList, int> >;
SkillLookup skillLookup(const std::map<QString, Library>& libraries);
//! The ratings from the progress log, as they were at the end of `asOf` (invalid: now)
std::map<QString, SkillRating> skillRatings(const Progress& progress, const SkillLookup& lookup = {}, const QDate& asOf = QDate());
QString skillName(const QString& id);
QString skillGroup(const QString& id);
QStringList skillGroups();   // in display order

// ---------------------------------------------------------------- plans
struct PlanSong {
    QString code;
    QString title;
    int set = 0;           // 1-based set number, 0 = none
    // per-area weights 0-3 (the library's "needs", which the user can change)
    std::map<QString, int> needs;
};

struct Block {
    QString code;          // song
    QString itemId;
    QString kind;          // "item", "warmup", "runthrough"
    int minutes = 0;
    int tempo = 0;         // the tempo to aim for today
    bool done = false;
    int tempoReached = 0;
    int stage = -1;        // stage set when done
};

struct Day {
    QDate date;
    int minutes = 0;
    QString label;         // e.g. "Run-through day", "Show day"
    std::vector<Block> blocks;
};

struct Plan {
    QString id;
    QString name;
    QString kind;          // "routine" or "plan"
    QDate created;
    QDate showDate;
    std::vector<PlanSong> songs;
    std::vector<Day> days;
    QString setlistId;     // the saved setlist it was made from
    int breakEvery = 0;    // minutes of practice between breaks (0: no breaks)
    int breakMinutes = 5;
    QJsonObject toJson() const;
    static Plan fromJson(const QJsonObject& o);
};

struct PlanRequest {
    QString name;
    QString kind = "routine";
    QDate showDate;
    std::vector<PlanSong> songs;
    std::vector<std::pair<QDate, int> > days;   // date, minutes ("routine": one day)
    //! More for a day already planned ("add N minutes"): its blocks so far (the new ones don't repeat them and count
    //! what they did), and no warm-up
    std::vector<Block> alreadyToday;
    bool noWarmup = false;
};

//! Builds the plan's days from today's progress. Days before `fromDate` (when given) are kept as they are.
Plan makePlan(const PlanRequest& request, const std::map<QString, Library>& libraries, const Progress& progress,
              const Plan* existing = nullptr, const QDate& fromDate = QDate());

//! More for a day already planned: `minutes` of new items at its end, from what the songs need after what the day
//! has (no warm-up). Returns the minutes added (0: nothing left to add).
int addToDay(Plan& plan, int dayIndex, int minutes, const std::map<QString, Library>& libraries, const Progress& progress);

//! The folder of songs to read libraries from: the library folder, or the automatic tests' folder
QString libraryRoot(const QString& libraryFolder);

//! Where practice data lives (progress.json, plans/, setlist.json)
QString practiceFolder();
std::vector<Plan> loadPlans();       // newest first
Plan loadPlan(const QString& id);
bool savePlan(const Plan& plan);
bool deletePlan(const QString& id);

//! iReal Pro: a web page with one link per item of the day (or the whole plan) and a link for them all as a playlist
QString irealHtml(const Plan& plan, const std::map<QString, Library>& libraries, int dayIndex = -1);

QString categoryName(const QString& category);
//! Chord names in text with real accidentals and symbols: "Bb^7" -> "B♭Δ7", "F#m7b5" -> "F♯ø7", "C7b9" -> "C7♭9"
QString prettyText(const QString& text);
QString formatMinutes(int minutes);
QString stageName(int stage);
}
