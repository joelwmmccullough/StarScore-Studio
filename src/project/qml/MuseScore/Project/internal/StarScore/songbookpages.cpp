/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — songbook cover, how-to, contents and song-opening pages
 */
#include "songbookpages.h"

#include <algorithm>

#include <QFont>
#include <QFontDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>

using namespace mu::project::songbook;

namespace {
// US Letter in points
constexpr double W = 612.0;
constexpr double H = 792.0;
constexpr double MX = 61.0;   // side margin (0.85 in)
constexpr double MY = 65.0;   // top margin (0.9 in)

const QColor PLUM("#3B1F5E");
const QColor LAV("#B9A6E8");
const QColor SOFT("#F3EFFA");
const QColor INK("#1D1A24");
const QColor GREY("#6F6A78");
const QColor RULE("#E3DDEE");

QFont headingFont(double size, bool bold = false)
{
    QFont f;
    f.setFamilies({ "TT Modernoir Trial", "TT Modernoir", "StarScore Jost", "Jost", "Helvetica Neue" });
    f.setPointSizeF(size);
    f.setBold(bold);
    return f;
}

QFont bodyFont(double size, bool bold = false)
{
    QFont f;
    f.setFamilies({ "StarScore Jost", "Jost", "Helvetica Neue", "Helvetica" });
    f.setPointSizeF(size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return f;
}

//! Draws wrapped text and returns the height it took
double text(QPainter& p, const QRectF& r, const QString& s, const QFont& f, const QColor& c, int flags = Qt::AlignLeft | Qt::AlignTop)
{
    p.setFont(f);
    p.setPen(c);
    QRectF used;
    p.drawText(r, flags | Qt::TextWordWrap, s, &used);
    return used.height();
}

void folio(QPainter& p, int number)
{
    text(p, QRectF(W - 0.6 * 72 - 60, H - 0.45 * 72 - 10, 60, 14), QString::number(number), bodyFont(9), GREY,
         Qt::AlignRight | Qt::AlignBottom);
}

void dots(QPainter& p, double x, double y, int on)
{
    for (int i = 0; i < 3; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(i < on ? PLUM : QColor("#D9D1EA"));
        p.drawEllipse(QRectF(x + i * 14, y, 10, 10));
    }
}

void drawCover(QPainter& p, const Book& b)
{
    p.fillRect(QRectF(0, 0, W, H), PLUM);
    // rings
    p.setBrush(Qt::NoBrush);
    QPen ring(QColor(185, 166, 232, 36), 28);
    p.setPen(ring);
    p.drawEllipse(QRectF(W - 260, 190, 360, 360));
    QPen ring2(QColor(185, 166, 232, 26), 18);
    p.setPen(ring2);
    p.drawEllipse(QRectF(W - 200, 250, 245, 245));

    QFont band = bodyFont(15);
    band.setLetterSpacing(QFont::PercentageSpacing, 150);
    text(p, QRectF(MX, MY, 300, 24), "STARSIGN", band, LAV);
    text(p, QRectF(MX - 3, MY + 30, W - 2 * MX, 110), b.album, headingFont(72, true), Qt::white);
    QFont book = bodyFont(13);
    book.setLetterSpacing(QFont::PercentageSpacing, 130);
    text(p, QRectF(MX, MY + 150, 300, 20), "THE SONGBOOK", book, LAV);

    const double instrY = H - 330;
    const double used = text(p, QRectF(MX - 2, instrY, W - 2 * MX, 180), b.instrument, headingFont(54, true), Qt::white);
    // key pill
    QFont pill = bodyFont(13, true);
    p.setFont(pill);
    const double pw = p.fontMetrics().horizontalAdvance(b.keyLabel) + 26;
    QPainterPath path;
    path.addRoundedRect(QRectF(MX, instrY + used + 14, pw, 26), 13, 13);
    p.fillPath(path, LAV);
    text(p, QRectF(MX, instrY + used + 14, pw, 26), b.keyLabel, pill, PLUM, Qt::AlignCenter);

    const QString tag = b.horn
                        ? QString("Every tune three ways: solo, as a duo, or as a trio.\nYour part fits with any horns your friends play.")
                        : QString("Every tune: the lead sheet, and the part written for your instrument.");
    text(p, QRectF(MX, H - 110, W - 2 * MX, 60), tag, bodyFont(16), Qt::white);
}

void drawHowTo(QPainter& p, const Book& b)
{
    double y = MY;
    y += text(p, QRectF(MX, y, W - 2 * MX, 60), "How to use this book", headingFont(34, true), PLUM) + 10;
    if (!b.horn) {
        y += text(p, QRectF(MX, y, W - 2 * MX, 200),
                  "Each song has two sheets. The lead sheet has the melody and the chord changes: learn the tune from it, "
                  "comp from it, or play the melody. Then comes the part written for " + b.instrument.toLower()
                  + " on the record, with the grooves, hits and figures the band plays.", bodyFont(12.5), INK) + 16;
        y += text(p, QRectF(MX, y, W - 2 * MX, 40), "Reading the parts", bodyFont(15, true), PLUM) + 6;
        text(p, QRectF(MX, y, W - 2 * MX, 200),
             "Rehearsal letters are the same in every Starsign Songbook, so a band reading from different books can start "
             "together from any letter. Chord symbols are written the same way throughout.", bodyFont(11.5), INK);
        return;
    }

    y += text(p, QRectF(MX, y, W - 2 * MX, 60),
              "Every song in this book comes three ways. Pick the one that matches how many horn players you have.",
              bodyFont(13), INK) + 16;

    struct Way { QString n, t, d; int dots; };
    const std::vector<Way> ways {
        { "1", "Solo", "The melody and chord changes. Play it alone, over the record, or with a rhythm section.", 1 },
        { "2", "Duo", "Two horns, a top line and a bottom line.", 2 },
        { "3", "Trio", "Three horns: top, middle and bottom lines.", 3 },
    };
    const double gap = 13;
    const double cw = (W - 2 * MX - 2 * gap) / 3;
    const double ch = 150;
    for (size_t i = 0; i < ways.size(); ++i) {
        const double x = MX + i * (cw + gap);
        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, cw, ch), 8, 8);
        p.fillPath(path, SOFT);
        text(p, QRectF(x + 13, y + 10, cw - 26, 40), ways[i].n, bodyFont(28, true), PLUM);
        text(p, QRectF(x + 13, y + 48, cw - 26, 20), ways[i].t, bodyFont(14, true), INK);
        text(p, QRectF(x + 13, y + 70, cw - 26, 60), ways[i].d, bodyFont(10.5), QColor("#3C3747"));
        dots(p, x + 13, y + ch - 22, ways[i].dots);
    }
    y += ch + 12;
    y += text(p, QRectF(MX, y, W - 2 * MX, 40), b.linesSummary, bodyFont(11.5, true), PLUM) + 14;

    y += text(p, QRectF(MX, y, W - 2 * MX, 30), "Playing with other horns", bodyFont(15, true), PLUM) + 6;
    y += text(p, QRectF(MX, y, W - 2 * MX, 100),
              "Every Starsign Songbook has the same lines, written for its own instrument. Put two or three books together "
              "and you have a horn section. The highest instrument takes the top line, the lowest takes the bottom. When "
              "two instruments could take the same line, pick either: every line is written to work.",
              bodyFont(11.5), INK) + 10;

    struct Row { QString line, books; };
    const std::vector<Row> rows {
        { "Top line", "Soprano Sax, Clarinet, Trumpet, Alto Sax, Violin, Flute (trio)" },
        { "Middle line (trio)", "Trumpet, Clarinet, Alto Sax, Tenor Sax, Viola" },
        { "Bottom line", "Tenor Sax, Bari Sax, Trombone, Bass Clarinet, Cello" },
    };
    const double c1 = 130;
    text(p, QRectF(MX + 6, y, c1, 18), "Line", bodyFont(10.5, true), PLUM);
    text(p, QRectF(MX + c1, y, W - 2 * MX - c1, 18), "Played from these books", bodyFont(10.5, true), PLUM);
    y += 20;
    for (const Row& r : rows) {
        const bool mine = std::any_of(b.lines.begin(), b.lines.end(), [&](const QString& l) {
            return l.contains(QString(r.line).remove(" (trio)"), Qt::CaseInsensitive);
        });
        if (mine) {
            p.fillRect(QRectF(MX, y - 3, c1 - 6, 22), SOFT);
        }
        text(p, QRectF(MX + 6, y, c1 - 12, 18), r.line, bodyFont(10.5, mine), INK);
        text(p, QRectF(MX + c1, y, W - 2 * MX - c1, 18), r.books, bodyFont(10.5), INK);
        y += 22;
        p.setPen(QPen(RULE, 1));
        p.drawLine(QPointF(MX, y - 4), QPointF(W - MX, y - 4));
    }
    y += 14;
    y += text(p, QRectF(MX, y, W - 2 * MX, 30), "Reading the parts", bodyFont(15, true), PLUM) + 6;
    text(p, QRectF(MX, y, W - 2 * MX, 120),
         "Everything is written for your instrument, chord symbols included. Rehearsal letters are the same in every book, so "
         "a duo or trio can start from any letter together. Learn each tune from its Solo sheet; the Duo and Trio sheets "
         "assume you know how it goes.", bodyFont(11.5), INK);
}

void drawContents(QPainter& p, const Book& b)
{
    double y = MY;
    y += text(p, QRectF(MX, y, W - 2 * MX, 60), "Contents", headingFont(34, true), PLUM) + 18;
    for (const Chapter& c : b.chapters) {
        text(p, QRectF(MX, y, 30, 22), QString::number(c.track), bodyFont(11), GREY);
        text(p, QRectF(MX + 32, y - 2, W - 2 * MX - 90, 24), c.song, bodyFont(14), INK);
        text(p, QRectF(W - MX - 60, y - 1, 60, 22), QString::number(c.openerPage), bodyFont(13, true), PLUM, Qt::AlignRight | Qt::AlignTop);
        y += 26;
        p.setPen(QPen(QColor("#ECE7F4"), 1));
        p.drawLine(QPointF(MX, y - 4), QPointF(W - MX, y - 4));
        y += 4;
    }
    if (!b.notInThisEdition.isEmpty()) {
        y += 16;
        text(p, QRectF(MX, y, W - 2 * MX, 60),
             "Not in this edition yet: " + b.notInThisEdition.join(", ") + ".", bodyFont(10), GREY);
    }
}
}

int mu::project::songbook::frontPageCount(const Book&)
{
    return 3;
}

static bool openWriter(QPdfWriter& writer, const QString& title)
{
    writer.setTitle(title);
    writer.setCreator("StarScore Studio");
    writer.setResolution(72);
    return writer.setPageLayout(QPageLayout(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
}

bool mu::project::songbook::writeFrontPages(const Book& book, const QString& pdfPath)
{
    QPdfWriter writer(pdfPath);
    openWriter(writer, book.album + " Songbook – " + book.instrument);
    QPainter p;
    if (!p.begin(&writer)) {
        return false;
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    drawCover(p, book);
    writer.newPage();
    drawHowTo(p, book);
    folio(p, 2);
    writer.newPage();
    drawContents(p, book);
    folio(p, 3);
    p.end();
    return true;
}

bool mu::project::songbook::writeOpener(const Book& book, const Chapter& c, const QString& pdfPath)
{
    QPdfWriter writer(pdfPath);
    openWriter(writer, c.song);
    QPainter p;
    if (!p.begin(&writer)) {
        return false;
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const double bandH = 190;
    p.fillRect(QRectF(0, 0, W, bandH), PLUM);
    QFont track = bodyFont(11);
    track.setLetterSpacing(QFont::PercentageSpacing, 130);
    text(p, QRectF(MX, 50, W - 2 * MX, 18), QString("%1 · TRACK %2").arg(book.album.toUpper()).arg(c.track), track, LAV);
    text(p, QRectF(MX - 2, 70, W - 2 * MX, 90), c.song, headingFont(46, true), Qt::white);
    text(p, QRectF(MX, bandH - 40, W - 2 * MX, 20), book.instrument, bodyFont(12), QColor("#E6DDFA"));

    double y = bandH + 30;
    if (!c.notes.trimmed().isEmpty()) {
        y += text(p, QRectF(MX, y, W - 2 * MX, 24), "Notes", bodyFont(15, true), PLUM) + 8;
        p.fillRect(QRectF(MX, y, 4, 4), LAV);
        const double used = text(p, QRectF(MX + 16, y, W - 2 * MX - 16, H - y - 260), c.notes, bodyFont(11.5), INK);
        p.fillRect(QRectF(MX, y, 4, used), LAV);
        y += used + 22;
    }

    y += text(p, QRectF(MX, y, W - 2 * MX, 24), "In this chapter", bodyFont(15, true), PLUM) + 10;
    const int n = int(c.sheets.size());
    const double gap = 10;
    const double cw = n > 0 ? (W - 2 * MX - (n - 1) * gap) / n : 0;
    for (int i = 0; i < n; ++i) {
        const ChapterSheet& s = c.sheets[i];
        const double x = MX + i * (cw + gap);
        QPainterPath path;
        path.addRoundedRect(QRectF(x, y, cw, 74), 7, 7);
        p.setPen(QPen(QColor("#E0D8EE"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        QFont k = bodyFont(9);
        k.setLetterSpacing(QFont::PercentageSpacing, 115);
        text(p, QRectF(x + 10, y + 9, cw - 20, 14), s.kind, k, GREY);
        text(p, QRectF(x + 10, y + 24, cw - 20, 32), s.title, bodyFont(12, true), INK);
        text(p, QRectF(x + 10, y + 54, cw - 20, 14), QString("page %1").arg(s.page), bodyFont(10), PLUM);
    }
    y += 74 + 24;

    if (book.horn) {
        text(p, QRectF(MX, y, W - 2 * MX, 24), "Who plays what", bodyFont(15, true), PLUM);
        y += 30;
        text(p, QRectF(MX, y, W - 2 * MX, 80), book.linesSummary, bodyFont(11.5), INK);
    }
    folio(p, c.openerPage);
    p.end();
    return true;
}
