/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the pages of a songbook that aren't music: cover, how to use it, contents, and each song's
 * opening page. Drawn with QPainter into PDFs (US Letter); headings in TT Modernoir, text in Jost.
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringList>

namespace mu::project::songbook {
struct ChapterSheet {
    QString kind;        // "SOLO", "DUO", "TRIO", "PART"
    QString title;       // "Melody and changes", "Bottom line", "Piano part"
    int page = 0;
};

struct Chapter {
    QString song;
    int track = 0;        // position on the album
    int openerPage = 0;
    QString notes;        // Joel's notes (may be empty)
    std::vector<ChapterSheet> sheets;
};

struct Book {
    QString album;        // "Ichiban"
    QString instrument;   // "Tenor Saxophone"
    QString keyLabel;     // "Parts in B♭"
    bool horn = true;     // horn books have Solo / Duo / Trio; rhythm books the lead sheet and a part
    QStringList lines;    // horn books: this instrument's lines, e.g. { "Duo · Bottom line", "Trio · Middle line", … }
    QString linesSummary; // "Tenor takes the bottom line in a duo, and the middle or bottom line in a trio."
    std::vector<Chapter> chapters;
    QStringList notInThisEdition;   // album songs left out (not ready)
};

//! Pages before the first song: cover, how to use this book, contents
int frontPageCount(const Book& book);

//! Writes the front pages (numbered from 1; the cover shows no number)
bool writeFrontPages(const Book& book, const QString& pdfPath);

//! Writes one song's opening page (with its page number)
bool writeOpener(const Book& book, const Chapter& chapter, const QString& pdfPath);
}
