/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: shared types and file helpers.
 *
 * The organizer keeps "Sheets and Demos" (the band's library) and "Projects and Sheets" (Joel's workspace)
 * in order and rebuilds every generated PDF in them. It replaces the Python toolkit that used to run as
 * two daily scheduled tasks (6 Inbox/.organizer and Projects and Sheets/.organizer).
 *
 * House rules it follows: nothing is ever deleted (files move to Version History / Deprecated / 5 Archive),
 * nothing inside Version History, Old Versions or 5 Archive is renamed, log entries are only ever added on top,
 * and the two folders never carry each other's information (except All Recordings.pdf).
 */
#pragma once

#include <atomic>
#include <functional>

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mu::project::starscore::org {
struct Paths {
    QString band;            // .../Sheets and Demos
    QString toolkit;         // band + "/6 Inbox/.organizer"
    QString projects;        // .../Projects and Sheets ("" when it can't be found)
    QString projToolkit;     // projects + "/.organizer"
    QDate today;

    static Paths make(const QString& band, const QString& projects, const QDate& today = QDate::currentDate());
    QString todayIso() const { return today.toString(Qt::ISODate); }
};

//! Messages for the progress window, and the Stop button
struct Progress {
    std::function<void(const QString& line)> log;
    std::function<void(const QString& step, double fraction)> step;
    std::atomic_bool* stop = nullptr;

    void say(const QString& line) const { if (log) { log(line); } }
    void at(const QString& s, double f) const { if (step) { step(s, f); } }
    bool stopped() const { return stop && stop->load(); }
};

// --- JSON files
QJsonDocument readJson(const QString& path, bool* ok = nullptr);
QJsonObject readJsonObject(const QString& path);
//! A read that tells a missing file from one that is there but can't be read (not openable, cloud-only placeholder,
//! broken JSON). The organizer's data files are kept forever, so "present but unreadable" must never be treated as
//! "missing" and rewritten from empty: callers stop the run instead.
struct JsonRead {
    QJsonDocument doc;
    bool exists = false;
    bool ok = false;          // exists and parsed
    QString error;            // why it couldn't be read (empty when ok or missing)
    bool unreadable() const { return exists && !ok; }
};
JsonRead readJsonChecked(const QString& path);
//! Written to a temporary file next to it, then swapped in (never half-written)
bool writeJson(const QString& path, const QJsonDocument& doc, bool compact = false);
bool writeText(const QString& path, const QByteArray& data);
//! codes.json the way the old organizer and StarScore's export write it: sorted keys, one-space indent, ASCII
QByteArray pythonStyleJson(const QJsonObject& o);

// --- Files (relative paths use '/')
//! The song title of a song folder: "1 Amplitudes" -> "Amplitudes", "4 Works In Progress/Jeju" -> "Jeju"
QString songTitleOf(const QString& songRoot);
QString joinPath(const QString& a, const QString& b);
QString relativeTo(const QString& base, const QString& path);
//! dst with " (2)", " (3)"… added before the extension until nothing is there
QString freeName(const QString& dst);
//! Move a file or folder, making the destination's folders. Never overwrites: returns false if dst exists.
bool moveItem(const QString& src, const QString& dst);
bool isAudio(const QString& fileName);
bool isBundle(const QString& path);   // .logicx, .mscbackup and other folders that are really one file
QString md5OfFile(const QString& path);
}
