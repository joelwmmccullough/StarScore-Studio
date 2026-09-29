/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — facts about Joel's Starsign songs shared by the Dashboard and Songbooks pages
 */
#pragma once

#include <map>
#include <set>
#include <vector>

#include <QRegularExpression>
#include <QString>

#include "project/istarscoreservice.h"

namespace mu::project::starscoresongs {
inline QString normalName(const QString& title)
{
    QString n = title.toLower();
    n.remove(QRegularExpression("[^a-z0-9]"));
    if (n == "feedyourkidsbugs") {
        n = "feedyourkidbugs";
    }
    return n;
}

//! Times played in 2026, from setlist.fm (the band folder's playcounts.py, fetched 19 Aug 2026)
inline int plays2026(const QString& title)
{
    static const std::map<QString, int> PLAYS = [] {
        const std::vector<std::pair<const char*, int> > raw {
            { "Amplitudes", 6 }, { "Dream of Mushroom", 6 }, { "Feed Your Kid Bugs", 6 }, { "The Courier", 6 },
            { "Hit List", 5 }, { "Seagrass", 5 }, { "Updog", 5 }, { "Cumulonimbus", 4 }, { "Double Entendre", 4 },
            { "Everpresent", 4 }, { "Industrial Park Driveway", 4 }, { "Okane", 4 }, { "Always There", 3 },
            { "Branston Pickle", 3 }, { "Fish Oil", 3 }, { "Shofukan", 3 }, { "Ciao Ferrari", 2 }, { "February", 2 },
            { "Last Pint", 2 }, { "Balkan Wedding", 1 }, { "Break Out", 1 }, { "Chameleon", 1 }, { "Little Louie", 1 },
            { "Live Strong + Strasbourg", 1 }, { "Pick Up The Pieces", 1 }, { "Royal", 1 }, { "Two", 1 },
            { "Up From the South", 1 },
        };
        std::map<QString, int> m;
        for (const auto& [name, n] : raw) {
            m[normalName(QString::fromUtf8(name))] = n;
        }
        return m;
    }();
    auto it = PLAYS.find(normalName(title));
    return it == PLAYS.end() ? 0 : it->second;
}

//! Songs in 1 Starsign Originals and 2 Starsign Covers on 29 Sep 2026, other than Top Hat, Bet and Another One:
//! they were converted from older files, so an arrangement counts as done once it's audited. Any other song
//! (the three above and every new one) counts as done once it's marked Finished.
inline bool isLegacy(const QString& title)
{
    static const std::set<QString> LEGACY = [] {
        const char* names[] = {
            "Amplitudes", "Branston Pickle", "Ciao Ferrari", "Cumulonimbus", "Deep Speech", "Deimos", "Double Entendre",
            "Dream of Mushroom", "Everpresent", "February", "Feed Your Kids Bugs", "Fish Oil", "G.I. Jorge", "Hit List",
            "Industrial Park Driveway", "Intern", "Last Pint", "Mxter Shirts", "Okane", "Porcupine", "Royal", "Seagrass",
            "Shatter", "The Courier", "Updog",
            "Always There", "Anarchy Rainbow", "Another Star", "Balkan Wedding", "Break Out", "Chameleon", "I'm A Spy",
            "Little Louie", "Move On Up", "Peasant Funk", "Pick Up The Pieces", "Playground", "Shofukan",
            "Standing Next to You", "The Chicken", "The Essential", "Two", "Up From the South",
        };
        std::set<QString> s;
        for (const char* n : names) {
            s.insert(normalName(QString::fromUtf8(n)));
        }
        return s;
    }();
    return LEGACY.count(normalName(title)) > 0;
}

//! Is this arrangement done? Songs converted from older files: audited (and unchanged since). Others: Finished.
inline bool arrangementDone(bool legacy, const StarScoreFileArrangement& a)
{
    return legacy ? a.audited : a.status == int(StarScoreStatus::Finished);
}

//! Is this section ready to print? It's Finished, or (older songs) an audited arrangement has it.
inline bool sectionDone(bool legacy, const StarScoreAuditFileSummary& s, const QString& sectionKey)
{
    auto it = s.sectionStatus.find(sectionKey);
    if (it == s.sectionStatus.end()) {
        return false;
    }
    if (!legacy || it->second == int(StarScoreStatus::Finished)) {
        return it->second == int(StarScoreStatus::Finished);
    }
    for (const StarScoreFileArrangement& a : s.arrangementList) {
        if (a.audited && a.sectionKeys.contains(sectionKey)) {
            return true;
        }
    }
    return false;
}
}
