/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: the practice book PDF (see starscorepracticebook.h)
 */
#include "starscorepracticebook.h"

#include <algorithm>
#include <memory>

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTextDocument>

using namespace mu::project::practice;

namespace {
constexpr double W = 612.0;   // US Letter, points
constexpr double H = 792.0;
constexpr double MX = 72.0;
constexpr double MTOP = 70.0;
constexpr double MBOT = 64.0;
constexpr double CW = W - 2 * MX;

const QColor BG("#16130F");
const QColor INK("#EDE6D6");
const QColor SOFT("#B9B0A0");
const QColor GREY("#8A8274");
const QColor GOLD("#CFA85E");
const QColor TEAL("#8FB3A8");
const QColor RULE("#3A342B");

QFont heading(double size)
{
    QFont f;
    f.setFamilies({ "TT Modernoir", "TT Modernoir Trial", "StarScore Jost", "Jost", "Helvetica Neue" });
    f.setPointSizeF(size);
    return f;
}

QFont body(double size, bool bold = false, bool italic = false)
{
    QFont f;
    f.setFamilies({ "StarScore Jost", "Jost", "Helvetica Neue", "Helvetica" });
    f.setPointSizeF(size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    f.setItalic(italic);
    return f;
}

QFont caps(double size, double spacing = 3.0)
{
    QFont f = body(size);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    return f;
}

QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

//! chord-ish text with real accidentals and symbols
QString pretty(const QString& s)
{
    return mu::project::practice::prettyText(s);
}

//! One thing on the page: a block of HTML text, an image, a rule, or a page break
struct Flow {
    enum Kind { Html, Image, Rule, Break, Space } kind = Html;
    QString html;
    QImage image;
    double height = 0;       // measured
    double space = 0;        // space above
    bool keepWithNext = false;
    QString footerTitle;     // for breaks: the running footer from here on
    int anchor = -1;         // day index whose page this starts (contents)
};

//! an image's size on the page: 160 dpi, no wider than the text, no taller than a page
QSizeF imageSize(const QImage& img)
{
    double w = std::min(CW, img.width() * 72.0 / 160.0);
    double h = w * img.height() / img.width();
    const double maxH = H - MTOP - MBOT - 20;
    if (h > maxH) {
        w *= maxH / h;
        h = maxH;
    }
    return QSizeF(w, h);
}

class Book
{
public:
    Book(QPdfWriter& w)
        : m_writer(w) {}

    QTextDocument* doc(const QString& html, double width) const
    {
        auto* d = new QTextDocument();
        d->setDocumentMargin(0);
        d->setDefaultFont(body(10.5));
        d->setDefaultStyleSheet(QString(
                                    "body{color:%1;} .goal{color:%2;font-style:italic;} .label{color:%3;font-weight:600;letter-spacing:2px;font-size:8pt;}"
                                    ".move{color:%2;font-style:italic;} .muted{color:%4;} .gold{color:%5;} td{color:%1;padding:3px 6px 3px 0;}"
                                    "th{color:%4;font-size:8pt;text-align:left;padding:2px 6px 4px 0;} h3{color:%1;font-size:12pt;margin:0;}"
                                    "li{margin-bottom:2px;}")
                                .arg(INK.name(), SOFT.name(), TEAL.name(), GREY.name(), GOLD.name()));
        d->documentLayout()->setPaintDevice(&m_writer);   // sizes as the PDF has them, not the screen
        d->setTextWidth(width);
        d->setHtml("<body>" + html + "</body>");
        return d;
    }

    double measure(Flow& f) const
    {
        if (f.kind == Flow::Html) {
            std::unique_ptr<QTextDocument> d(doc(f.html, CW));
            f.height = d->size().height();
        } else if (f.kind == Flow::Image && !f.image.isNull()) {
            f.height = imageSize(f.image).height();
        } else if (f.kind == Flow::Rule) {
            f.height = 8;
        } else if (f.kind == Flow::Space) {
            f.height = f.space;
            f.space = 0;
        }
        return f.height;
    }

    QPdfWriter& m_writer;
};

void pageBackground(QPainter& p)
{
    p.fillRect(QRectF(0, 0, W, H), BG);
}

void footer(QPainter& p, const QString& title, int page)
{
    p.setFont(body(7.5));
    p.setPen(GREY);
    p.drawText(QRectF(MX, H - 44, CW, 14), Qt::AlignLeft | Qt::AlignVCenter, title);
    p.setFont(heading(10));
    p.setPen(GOLD);
    p.drawText(QRectF(MX, H - 46, CW, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(page));
}

void doubleRule(QPainter& p, double cx, double y, double w)
{
    p.setPen(QPen(GOLD, 0.9));
    p.drawLine(QPointF(cx - w / 2, y), QPointF(cx + w / 2, y));
    p.drawLine(QPointF(cx - w / 2, y + 3), QPointF(cx + w / 2, y + 3));
}

void drawCaps(QPainter& p, const QRectF& r, const QString& s, double size, const QColor& c, double spacing = 3.0)
{
    p.setFont(caps(size, spacing));
    p.setPen(c);
    p.drawText(r, Qt::AlignHCenter | Qt::AlignVCenter, s);
}
}

bool mu::project::practice::writePracticeBook(const Plan& plan, const std::map<QString, Library>& libs, const QString& pdfPath,
                                              const NotationRenderer& renderer, int dayIndex,
                                              const std::function<void(int, int)>& progress)
{
    QPdfWriter writer(pdfPath);
    writer.setTitle(plan.name);
    writer.setCreator("StarScore Studio");
    writer.setPageSize(QPageSize(QPageSize::Letter));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    writer.setResolution(72);
    QPainter p;
    if (!p.begin(&writer)) {
        return false;
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    Book book(writer);

    std::vector<int> days;
    for (int i = 0; i < int(plan.days.size()); ++i) {
        if (dayIndex < 0 || i == dayIndex) {
            days.push_back(i);
        }
    }
    int totalMinutes = 0;
    int totalBlocks = 0;
    for (int i : days) {
        totalMinutes += plan.days[i].minutes;
        totalBlocks += int(plan.days[i].blocks.size());
    }
    std::map<int, int> setCount;
    for (const PlanSong& s : plan.songs) {
        setCount[s.set] += 1;
    }

    // ---- the flow: everything after the front pages
    std::vector<Flow> flow;
    auto html = [&](const QString& h, double space = 0, bool keep = false) {
        Flow f;
        f.kind = Flow::Html;
        f.html = h;
        f.space = space;
        f.keepWithNext = keep;
        flow.push_back(f);
    };
    auto pageBreak = [&](const QString& footerTitle, int anchor = -1) {
        Flow f;
        f.kind = Flow::Break;
        f.footerTitle = footerTitle;
        f.anchor = anchor;
        flow.push_back(f);
    };

    // About this plan
    pageBreak(plan.name);
    {
        QString h = "<p class='label' align='center'>BEFORE YOU START</p>"
                    "<p align='center' style='font-size:24pt;'><span style=\"font-family:'TT Modernoir','TT Modernoir Trial','Jost';\">About this plan</span></p><br>";
        const bool isPlan = plan.kind == "plan";
        h += QString("<p>%1 %2 of practice for %3 song%4%5. %6, then works through the songs in set order. Every block says "
                     "what to play, the tempo to start at and the tempo to reach, and when it's done: "
                     "<span class='move'>move on when</span> you can do what that line says, without hesitation.</p>")
             .arg(days.size() == 1 ? QString("One session,") : QString("%1 days,").arg(days.size()))
             .arg(esc(formatMinutes(totalMinutes)))
             .arg(plan.songs.size())
             .arg(plan.songs.size() == 1 ? "" : "s")
             .arg(isPlan && plan.showDate.isValid() ? QString(", up to the show on %1").arg(plan.showDate.toString("dddd d MMMM")) : QString())
             .arg(days.size() == 1 ? QString("It starts with a short warm-up when there's time for one")
                                   : QString("Each day starts with a short warm-up when there's time for one"));
        h += isPlan ? "<p>Mark each block in StarScore's practice runner as you finish it, with the tempo you reached and how solid it "
                      "is. <i>Plan again from today</i> rebuilds the days still to come from what you actually did, so items that "
                      "aren't solid yet come back, and the run-through day before the show plays every song through.</p>"
                    : "<p>Mark each block in StarScore's practice runner as you finish it, with the tempo you reached and how solid it "
                      "is. The next routine you make starts from there.</p>";
        html(h);
        html("<br><p class='label'>THE SONGS</p>", 0, true);
        // the setlist, a row at a time (so a long setlist runs onto the next page)
        int curSet = -1;
        for (const PlanSong& s : plan.songs) {
            QString row;
            if (s.set != curSet) {
                curSet = s.set;
                if (s.set > 0) {
                    row += QString("<p class='gold' style='margin-top:6px;'>SET %1</p>").arg(s.set);
                }
            }
            auto lib = libs.find(s.code);
            int songMin = 0;
            for (int i : days) {
                for (const Block& b : plan.days[i].blocks) {
                    if (b.code == s.code) {
                        songMin += b.minutes;
                    }
                }
            }
            row += QString("<table width='100%' cellspacing='0'><tr><td width='28%'>%1</td><td width='58%' class='muted'>%2</td>"
                           "<td width='14%' align='right' class='muted'>%3</td></tr></table>")
                   .arg(esc(s.title), lib == libs.end() ? QString("no library yet") : esc(lib->second.style),
                        songMin ? esc(formatMinutes(songMin)) : QString("&mdash;"));
            html(row);
        }
    }

    // the days
    int blockNo = 0;
    for (int di : days) {
        const Day& d = plan.days[di];
        const QString dayTitle = d.date.isValid() ? d.date.toString("dddd d MMMM") : QString("Session");
        pageBreak(QString("%1 \u00B7 %2").arg(plan.name, dayTitle), di);
        QString h = QString("<p class='label' align='center'>%1</p>").arg(days.size() > 1 ? QString("DAY %1").arg(di + 1) : QString("PRACTICE ROUTINE"));
        h += QString("<p align='center' style='font-size:26pt;'><span style=\"font-family:'TT Modernoir','TT Modernoir Trial','Jost';\">%1</span></p>").arg(esc(dayTitle));
        html(h);
        {
            Flow r;
            r.kind = Flow::Rule;
            flow.push_back(r);
        }
        QString sum = QString("<p align='center' class='goal'>%1%2</p>").arg(esc(formatMinutes(d.minutes)),
                                                                          d.label.isEmpty() ? QString() : QString(" \u00B7 %1").arg(esc(d.label)));
        html(sum, 4);
        // the day's table
        QString t = "<table width='100%' cellspacing='0'><tr><th>#</th><th>SONG</th><th>ITEM</th><th align='right'>MIN</th><th align='right'>&#9833;</th></tr>";
        int n = 0;
        for (const Block& b : d.blocks) {
            ++n;
            auto lib = libs.find(b.code);
            const Item* it = lib == libs.end() ? nullptr : lib->second.item(b.itemId);
            t += QString("<tr><td class='gold'>%1</td><td>%2</td><td>%3%4</td><td align='right'>%5</td><td align='right' class='muted'>%6</td></tr>")
                 .arg(n)
                 .arg(lib == libs.end() ? esc(b.code) : esc(lib->second.title))
                 .arg(it ? esc(pretty(it->title)) : esc(b.itemId))
                 .arg(b.kind == "warmup" ? QString(" <span class='muted'>(warm-up)</span>") : QString())
                 .arg(b.minutes)
                 .arg(b.tempo > 0 ? QString::number(b.tempo) : QString());
        }
        t += "</table>";
        html(t, 10);

        n = 0;
        QString lastSong;
        for (const Block& b : d.blocks) {
            ++n;
            ++blockNo;
            if (progress) {
                progress(blockNo, totalBlocks);
            }
            auto lib = libs.find(b.code);
            if (lib == libs.end()) {
                continue;
            }
            const Item* it = lib->second.item(b.itemId);
            if (!it) {
                continue;
            }
            QString bh;
            if (b.code != lastSong) {
                lastSong = b.code;
                bh += QString("<p style='font-size:15pt;margin-top:6px;'><span style=\"font-family:'TT Modernoir','TT Modernoir Trial','Jost';\" class='gold'>%1</span></p>")
                      .arg(esc(lib->second.title));
            }
            QString tag = categoryName(it->category);
            if (!it->solo.isEmpty()) {
                if (const Solo* s = lib->second.solo(it->solo)) {
                    tag += " \u00B7 " + s->name;
                }
            }
            bh += QString("<h3><span class='gold'>%1.</span>&nbsp; %2</h3><p class='muted' style='font-size:8.5pt;'>%3 \u00B7 %4 min%5</p>")
                  .arg(n).arg(esc(pretty(it->title)), esc(pretty(tag))).arg(b.minutes)
                  .arg(it->level ? QString(" \u00B7 level %1").arg(it->level) : QString());
            if (!it->goal.isEmpty()) {
                bh += QString("<p class='goal'>%1</p>").arg(esc(pretty(it->goal)));
            }
            if (!it->steps.isEmpty()) {
                bh += "<ol style='margin-top:2px;margin-bottom:2px;'>";
                for (const QString& s : it->steps) {
                    bh += QString("<li>%1</li>").arg(esc(pretty(s)));
                }
                bh += "</ol>";
            }
            QStringList facts;
            if (it->tempoStart > 0) {
                facts << QString("Tempo %1 \u2192 %2").arg(it->tempoStart).arg(std::max(it->tempoGoal, it->tempoStart))
                      + (b.tempo > 0 ? QString(", today %1").arg(b.tempo) : QString());
            }
            if (!it->refs.isEmpty()) {
                facts << "In the score: " + it->refs.join("; ");
            }
            if (!it->listen.isEmpty()) {
                facts << "Listen: " + it->listen;
            }
            if (!facts.isEmpty()) {
                bh += QString("<p class='muted' style='font-size:9pt;'>%1</p>").arg(esc(pretty(facts.join("  \u00B7  "))));
            }
            if (!it->moveOnWhen.isEmpty()) {
                bh += QString("<p><span class='label'>MOVE ON WHEN</span>&nbsp;&nbsp;<span class='move'>%1</span></p>").arg(esc(pretty(it->moveOnWhen)));
            }
            const bool hasImage = renderer && it->hasXml();
            html(bh, 12, hasImage);
            if (hasImage) {
                const QList<QImage> images = renderer(*it, b.tempo);
                for (const QImage& img : images) {
                    if (img.isNull()) {
                        continue;
                    }
                    Flow f;
                    f.kind = Flow::Image;
                    f.image = img;
                    f.space = 4;
                    flow.push_back(f);
                }
            }
        }
    }

    // ---- paginate: front pages are the cover (1) and contents (2); the flow starts on page 3
    for (Flow& f : flow) {
        book.measure(f);
    }
    struct Placed {
        int page;
        double y;
    };
    std::vector<Placed> placed(flow.size());
    std::map<int, int> dayPage;
    std::vector<QString> footers;   // per page index (from 3)
    int page = 2;
    double y = H;   // forces a page at the first break
    QString curFooter = plan.name;
    for (size_t i = 0; i < flow.size(); ++i) {
        Flow& f = flow[i];
        if (f.kind == Flow::Break) {
            ++page;
            y = MTOP;
            curFooter = f.footerTitle;
            if (f.anchor >= 0) {
                dayPage[f.anchor] = page;
            }
            placed[i] = { page, y };
            footers.resize(page + 1);
            footers[page] = curFooter;
            continue;
        }
        double need = f.space + f.height;
        if (f.keepWithNext && i + 1 < flow.size() && flow[i + 1].kind == Flow::Html) {
            need += flow[i + 1].space + std::min(flow[i + 1].height, 120.0);
        } else if (f.keepWithNext && i + 1 < flow.size() && flow[i + 1].kind == Flow::Image) {
            // the text and its notation on one page when they fit on one
            const double room = H - MBOT - MTOP - f.height - f.space;
            need += flow[i + 1].space + std::max(0.0, std::min(flow[i + 1].height, room));
        }
        if (y + need > H - MBOT && y > MTOP + 1) {
            ++page;
            y = MTOP;
            footers.resize(page + 1);
            footers[page] = curFooter;
        } else {
            y += f.space;
        }
        placed[i] = { page, y };
        y += f.height;
    }
    const int lastPage = page;

    // ---- draw: cover
    pageBackground(p);
    {
        p.setPen(QPen(GOLD, 0.8));
        p.drawRect(QRectF(46, 46, W - 92, H - 92));
        p.drawRect(QRectF(49, 49, W - 98, H - 98));
        drawCaps(p, QRectF(0, 250, W, 16), plan.kind == "plan" ? "PRACTICE PLAN \u00B7 KEYS" : "PRACTICE ROUTINE \u00B7 KEYS", 8.5, GOLD, 3.5);
        p.setFont(heading(40));
        p.setPen(INK);
        p.drawText(QRectF(60, 280, W - 120, 120), Qt::AlignCenter | Qt::TextWordWrap, plan.name);
        doubleRule(p, W / 2, 410, 90);
        QString sub = QString("%1 song%2 \u00B7 %3").arg(plan.songs.size()).arg(plan.songs.size() == 1 ? "" : "s").arg(formatMinutes(totalMinutes));
        if (setCount.size() > 1 || (setCount.size() == 1 && setCount.begin()->first > 0)) {
            int sets = 0;
            for (const auto& [s, c] : setCount) {
                sets += s > 0 ? 1 : 0;
            }
            if (sets > 0) {
                sub += QString(" \u00B7 %1 set%2").arg(sets).arg(sets > 1 ? "s" : "");
            }
        }
        if (plan.kind == "plan" && plan.showDate.isValid()) {
            sub += QString("\nfor the show on %1").arg(plan.showDate.toString("dddd d MMMM yyyy"));
        }
        p.setFont(body(12.5, false, true));
        p.setPen(SOFT);
        p.drawText(QRectF(80, 430, W - 160, 60), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, sub);
        p.setPen(QPen(RULE, 0.6));
        p.drawLine(QPointF(W / 2 - 25, 640), QPointF(W / 2 + 25, 640));
        p.setFont(body(7.5));
        p.setPen(GREY);
        p.drawText(QRectF(0, 652, W, 40), Qt::AlignHCenter | Qt::AlignTop,
                   QString("Made by StarScore Studio \u00B7 %1\nDark edition").arg(QDate::currentDate().toString("d MMMM yyyy")));
    }
    // contents
    writer.newPage();
    pageBackground(p);
    {
        p.setFont(heading(30));
        p.setPen(INK);
        p.drawText(QRectF(0, 80, W, 50), Qt::AlignHCenter | Qt::AlignVCenter, "Contents");
        doubleRule(p, W / 2, 140, 80);
        double cy = 175;
        auto line = [&](const QString& left, int pg, bool italic) {
            p.setFont(body(11, false, italic));
            p.setPen(INK);
            const QFontMetricsF fm(p.font());
            p.drawText(QPointF(MX, cy), left);
            const double lw = fm.horizontalAdvance(left);
            p.setPen(QPen(RULE, 0.6, Qt::DotLine));
            p.drawLine(QPointF(MX + lw + 6, cy - 2), QPointF(W - MX - 22, cy - 2));
            p.setFont(heading(11));
            p.setPen(GOLD);
            p.drawText(QRectF(W - MX - 30, cy - 12, 30, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(pg));
            cy += 24;
        };
        line("About this plan", 3, true);
        for (int di : days) {
            const Day& d = plan.days[di];
            QString l = days.size() > 1 ? QString("Day %1 \u00B7 %2").arg(di + 1).arg(d.date.toString("ddd d MMM")) : d.date.toString("dddd d MMMM");
            l += QString(" \u00B7 %1").arg(formatMinutes(d.minutes));
            if (!d.label.isEmpty()) {
                l += " \u00B7 " + d.label;
            }
            if (cy > H - MBOT - 20) {
                break;
            }
            line(l, dayPage.count(di) ? dayPage[di] : 0, false);
        }
        footer(p, plan.name, 2);
    }
    // the flow
    int curPage = 2;
    for (size_t i = 0; i < flow.size(); ++i) {
        const Flow& f = flow[i];
        const Placed& at = placed[i];
        while (curPage < at.page) {
            writer.newPage();
            ++curPage;
            pageBackground(p);
            footer(p, curPage < int(footers.size()) ? footers[curPage] : plan.name, curPage);
        }
        if (f.kind == Flow::Html) {
            std::unique_ptr<QTextDocument> d(book.doc(f.html, CW));
            p.save();
            p.translate(MX, at.y);
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.palette.setColor(QPalette::Text, INK);
            d->documentLayout()->draw(&p, ctx);
            p.restore();
        } else if (f.kind == Flow::Image && !f.image.isNull()) {
            const QSizeF sz = imageSize(f.image);
            p.drawImage(QRectF(MX + (CW - sz.width()) / 2, at.y, sz.width(), sz.height()), f.image);
        } else if (f.kind == Flow::Rule) {
            doubleRule(p, W / 2, at.y + 2, 80);
        }
    }
    (void)lastPage;
    p.end();
    return true;
}
