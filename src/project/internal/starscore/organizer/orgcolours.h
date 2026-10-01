/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: Finder colours of the song folders and their sheet folders.
 *
 * They come from the sheet records StarScore writes at every export (6 Inbox/.organizer/sheets/CODE.json; see
 * starscoresheetrecord.cpp): each sheet's status when it was exported, and which sheets each colour needs. A sheet
 * counts only while the file in the folder is still the one exported (same size and md5).
 *
 * Song folder, from the top (each colour also needs everything the ones below it need):
 *   Purple  the Big Band and Marching Band charts finished and exported
 *   Blue    every sheet the songbooks and the 4- to 7-Horn charts need
 *   Green   every horn sheet of 1-Horn, 2-Horn Flexible, 2-Horn Standard, 3-Horn Flexible, 3-Horn Standard, 4-Horn Standard
 *   Yellow  the 3-Horn Section, drums, guitar, bass, keys and the lead sheet
 *   Orange  the 3-Horn Section, guitar, bass and the lead sheet
 *   Red     the 3-Horn Section, drums, guitar, bass, keys and the lead sheet exported at least as Sketch
 *   Gray    anything less
 * "Finished" except for Red. Drums or keys that read the lead sheet count as the lead sheet. Percussion is left out.
 * A song with no record yet (not exported from StarScore 1.14 or later) gets no colour.
 *
 * Sheet folder (1 Lead Sheet, 1 Rhythm, each horn folder, Big Band …): Gray when a sheet it must have is missing;
 * otherwise by the least finished file in it: Green (all Finished), Yellow (Needs review), Orange (In progress),
 * Red (Sketch), Gray (a file with no status, e.g. not exported from StarScore, or changed since).
 */
#pragma once

#include <map>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "orgcore.h"
#include "orgscan.h"

namespace mu::project::starscore::org {
//! Every song's sheet record, by code
std::map<QString, QJsonObject> loadSheetRecords(const Paths& paths);

struct SongColours {
    QString song;                                          // "Green" …, or "Gray"
    std::vector<std::pair<QString, QString> > folders;     // sheet folder (relative to the song folder), colour
    QStringList why;                                       // what keeps the song from the next colour up
};

SongColours songColours(const Paths& paths, const QString& songRoot, const QJsonObject& record,
                        const std::map<QString, SheetEntry>& sheets);
}
