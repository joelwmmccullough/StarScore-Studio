/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: shared HTML pieces, and each song's "What's Here" page (the old gen_pdfs.py)
 */
#include "orghtml.h"

#include <QLocale>
#include <QRegularExpression>

namespace mu::project::starscore::org {
QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

QString prettyDate(const QString& iso)
{
    const QDate d = QDate::fromString(iso.left(10), Qt::ISODate);
    if (!d.isValid()) {
        return esc(iso);
    }
    return QLocale(QLocale::English).toString(d, "d MMM yyyy");
}

QString hhmmss(int s)
{
    const int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    return h ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'))
           : QString("%1:%2").arg(m).arg(sec, 2, 10, QChar('0'));
}

QString plural(int n, const QString& one, const QString& many)
{
    return QString("%1 %2").arg(n).arg(n == 1 ? one : (many.isEmpty() ? one + "s" : many));
}

QString thousands(int n)
{
    return QLocale(QLocale::English).toString(n);
}

const char* BASE_CSS = R"CSS(
*{box-sizing:border-box;}
html{zoom:0.9375;}
body{font-family:'Helvetica Neue',Helvetica,Arial,sans-serif;color:#1b2030;margin:0;
     font-size:10.2pt;line-height:1.42;-webkit-print-color-adjust:exact;print-color-adjust:exact;}
h1{font-size:23pt;margin:0;letter-spacing:-.4px;font-weight:700;color:#16213e;}
h2{font-size:12.5pt;margin:19px 0 7px;color:#16213e;font-weight:700;
   border-bottom:2px solid #16213e;padding-bottom:3px;letter-spacing:.2px;break-after:avoid;}
h3{font-size:10.6pt;margin:12px 0 3px;font-weight:700;color:#1b2030;break-after:avoid;}
.sub{color:#6d7387;font-size:9.6pt;margin-top:3px;}
.hdr{display:flex;justify-content:space-between;align-items:flex-start;
     border-bottom:3px solid #16213e;padding-bottom:9px;}
.code{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:19pt;font-weight:700;
      color:#fff;background:#16213e;padding:5px 13px;border-radius:5px;letter-spacing:1px;}
.chips{margin:11px 0 2px;}
.chip{display:inline-block;background:#eef1f7;border:1px solid #d8def0;border-radius:20px;
      padding:3px 11px;margin:0 5px 5px 0;font-size:9pt;color:#33405e;}
.chip b{color:#16213e;}
.chip.on{background:#e6f3ec;border-color:#bfe0cd;color:#1d6144;}
.chip.off{background:#f6f2e8;border-color:#e7dcc2;color:#7a6320;}
table{width:100%;border-collapse:collapse;margin:5px 0 2px;}
tr{break-inside:avoid;}
th{text-align:left;font-size:8.4pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;
   font-weight:700;padding:0 8px 4px 0;border-bottom:1px solid #dfe3ec;}
td{padding:5px 8px 5px 0;border-bottom:1px solid #f0f2f7;vertical-align:top;font-size:9.7pt;}
tr:last-child td{border-bottom:none;}
.fold{font-weight:700;color:#16213e;white-space:nowrap;}
.mono{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:9pt;}
.dim{color:#6d7387;}
.note{background:#f7f8fb;border-left:3px solid #c8a24a;padding:8px 11px;margin:11px 0;
      font-size:9.3pt;color:#3d4356;border-radius:0 4px 4px 0;break-inside:avoid;}
.arr{margin:7px 0 0;padding:8px 11px;background:#f7f8fb;border-radius:5px;
     border-left:3px solid #16213e;break-inside:avoid;}
.arr .nm{font-family:'SF Mono',Menlo,Consolas,monospace;font-weight:700;color:#16213e;font-size:10pt;}
.arr .ins{color:#3d4356;font-size:9.4pt;margin-top:2px;}
.arr .fl{color:#6d7387;font-size:8.7pt;margin-top:3px;}
.foot{margin-top:16px;padding-top:7px;border-top:1px solid #dfe3ec;
      color:#8b90a0;font-size:8.2pt;display:flex;justify-content:space-between;gap:12px;}
.pagebreak{page-break-after:always;break-after:page;}
ul{margin:3px 0 3px 17px;padding:0;} li{margin:1.5px 0;}
.grid2{display:flex;gap:22px;} .grid2>div{flex:1;}
.k{display:inline-block;font-family:'SF Mono',Menlo,Consolas,monospace;background:#16213e;color:#fff;
   border-radius:3px;padding:1px 6px;font-size:9pt;font-weight:700;margin-right:6px;}
.big{font-size:31pt;font-weight:700;color:#16213e;line-height:1;}
.tile{background:#f7f8fb;border:1px solid #e3e7f0;border-radius:7px;padding:11px 13px;flex:1;}
.tile .lab{font-size:8.2pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;margin-top:4px;}
.bar{height:11px;background:#e8ebf2;border-radius:6px;overflow:hidden;margin:5px 0 2px;display:flex;}
.bar i{display:block;height:100%;}
.s-done{background:#2f7d5c;} .s-part{background:#c8a24a;} .s-none{background:#dde1ea;}
.dot{display:inline-block;width:12px;height:12px;border-radius:3px;vertical-align:-1px;}
td.c{text-align:center;padding:4px 2px;}
th.c{text-align:center;padding-right:0;}
.mtx td{font-size:9.3pt;padding:3.5px 4px;}
.mtx th{font-size:8pt;}
.legend{font-size:8.8pt;color:#6d7387;margin-top:7px;}
.legend span{margin-right:15px;}
)CSS";

QString htmlPage(const QString& title, const QString& body, const QString& extraCss, const QString& margins)
{
    return QString("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title>"
                   "<style>@page { size: letter; margin: %2; }%3%4</style></head><body>%5</body></html>")
           .arg(esc(title), margins, QString::fromUtf8(BASE_CSS), extraCss, body);
}

// ------------------------------------------------------------------ What's Here
static const QMap<QString, QString> FOLDER_BLURB {
    { "1 Lead Sheet", "The tune itself &mdash; melody, chords and form. If you only grab one sheet, grab this." },
    { "1 Rhythm", "Bass, drums, percussion, keys and guitar parts." },
    { "Extras", "Strings, guest parts, alternate versions and anything that is not a horn or rhythm part." },
    { "Demos", "Audio for the tune &mdash; reference recordings and rehearsal tracks." },
    { "Reference PDFs", "Reference material Joel keeps with the tune: transcriptions, original charts, notes." },
    { "Version History", "Every past version of this song, kept forever. Nothing here is current &mdash; do not play from it." },
    { "Horn Part Guides", "One PDF per horn player explaining exactly how their parts differ between arrangements." },
    { "Update Notes", "Per-player changelogs &mdash; what changed on your sheet, newest first." },
};

static QString stripCode(const QString& file, const QString& code)
{
    QString s = file;
    s.remove(QRegularExpression("^" + QRegularExpression::escape(code) + " - "));
    s.remove(QRegularExpression("\\.pdf$"));
    return s;
}

QString whatsHereHtml(const SongInfo& d, const Roster& roster, const QDate& today)
{
    const QString code = d.code;
    static const QMap<QString, QString> CATS { { "orig", "Starsign original" }, { "cover", "Cover" },
        { "vocal", "Cover (needs a vocalist)" }, { "wip", "Work in progress" } };
    static const QMap<QString, QString> FULL_TITLE { { "Live Strong + Strasbourg", "Live Strong / Strasbourg / St. Denis (medley)" } };
    const QString full = FULL_TITLE.value(d.name);
    const QString red = "#a8391f", gold = "#8a6d1f";

    QStringList chips;
    const QString lq = d.status.lead;
    chips << QString("<span class=\"chip %1\">Lead sheet: <b>%2</b></span>").arg(lq == "done" ? "on" : "off",
                                                                                 lq == "done" ? "yes" : lq == "partial" ? "blank template" : "not yet");
    int nblank = 0;
    for (const auto& [p, q] : d.rhythmq) {
        nblank += q == Quality::Empty;
    }
    chips << QString("<span class=\"chip %1\">Rhythm parts: <b>%2</b>%3</span>")
        .arg(d.status.rhythm == "done" ? "on" : "off",
             d.rhythm.isEmpty() ? QString("none") : QString::number(d.rhythm.size()),
             nblank ? QString(" <span style=\"color:%1;\">(%2 blank)</span>").arg(red).arg(nblank) : QString());
    int real = 0, stubs = 0;
    for (const ArrangementInfo& a : d.arrangements) {
        if (!a.scoreOnly && a.usable && a.emptyParts.isEmpty()) {
            ++real;
        }
        if (!a.emptyParts.isEmpty()) {
            ++stubs;
        }
    }
    chips << QString("<span class=\"chip %1\">Horn arrangements: <b>%2</b>%3</span>")
        .arg(real ? "on" : "off", real ? QString::number(real) : QString("none"),
             stubs ? QString(" <span style=\"color:%1;\">(+%2 blank)</span>").arg(red).arg(stubs) : QString());
    if (!d.demos.isEmpty()) {
        chips << QString("<span class=\"chip\">Demo audio: <b>%1</b></span>").arg(d.demos.size());
    }
    if (d.archive) {
        chips << QString("<span class=\"chip\">Archived files: <b>%1</b></span>").arg(thousands(d.archive));
    }

    QString b;
    b += QString("<div class=\"hdr\"><div><h1>%1</h1><div class=\"sub\">%2%3 &middot; every file in this folder starts with "
                 "<b>%4</b></div></div><div class=\"code\">%4</div></div>")
         .arg(esc(d.name), CATS.value(d.cat), full.isEmpty() ? QString() : " &middot; " + esc(full), code);
    b += "<div class=\"chips\">" + chips.join("") + "</div>";

    struct Row {
        QString folder, what, contents;
    };
    std::vector<Row> rows;
    auto names = [&](const QStringList& files) {
        QStringList out;
        for (const QString& f : files) {
            out << esc(stripCode(f, code));
        }
        return out.join(", ");
    };
    if (!d.lead.isEmpty()) {
        rows.push_back({ "1 Lead Sheet", FOLDER_BLURB["1 Lead Sheet"], names(d.lead) });
    }
    if (!d.rhythm.isEmpty()) {
        QStringList lab;
        for (const QString& r : d.rhythm) {
            const Quality q = d.rhythmq.count(r) ? d.rhythmq.at(r) : Quality::Ok;
            lab << esc(r) + (q == Quality::Empty ? QString(" <b style=\"color:%1;\">(blank)</b>").arg(red)
                             : q == Quality::Thin ? QString(" <b style=\"color:%1;\">(short)</b>").arg(gold) : QString());
        }
        rows.push_back({ "1 Rhythm", FOLDER_BLURB["1 Rhythm"], lab.join(", ") });
    }
    for (const ArrangementInfo& a : d.arrangements) {
        QString what;
        if (a.solo) {
            what = "One-horn versions &mdash; the whole tune on a single horn part, one sheet per instrument. "
                   "Pick the sheet for your horn.";
        } else if (a.generic) {
            what = QString("%1 horns, any instruments &mdash; pick the part in your key.").arg(a.n);
        } else {
            what = QString("%1 horns: %2").arg(a.n).arg(esc(a.instruments.join(", ")));
        }
        if (a.scoreOnly) {
            what += " &nbsp;<i>(full score only &mdash; individual parts not written yet)</i>";
        }
        QString cnt = plural(int(a.files.size()), "file");
        if (!a.emptyParts.isEmpty()) {
            cnt += QString(" <b style=\"color:%1;\">&mdash; %2 blank</b>").arg(red).arg(a.emptyParts.size());
        }
        rows.push_back({ a.folder, what, cnt });
    }
    for (const EnsembleInfo& e : d.ensembles) {
        rows.push_back({ e.folder, esc(e.note.isEmpty() ? QString("Additional ensemble arrangement.") : e.note),
                         plural(int(e.files.size()), "file") });
    }
    if (!d.extras.isEmpty()) {
        QString x = names(d.extras);
        if (x.size() > 150) {
            x = x.left(147) + "&hellip;";
        }
        rows.push_back({ "Extras", FOLDER_BLURB["Extras"], x });
    }
    if (!d.demos.isEmpty()) {
        rows.push_back({ "Demos", FOLDER_BLURB["Demos"], names(d.demos) });
    }
    if (!d.references.isEmpty()) {
        rows.push_back({ "Reference PDFs", FOLDER_BLURB["Reference PDFs"], plural(int(d.references.size()), "file") });
    }
    for (const auto& [k, v] : d.other) {
        rows.push_back({ k, "Additional material.", plural(int(v.size()), "file") });
    }
    QStringList guidePlayers;
    for (const QString& g : d.hornGuides) {
        const QString who = stripCode(g, code);
        for (const Player* p : roster.currentHorns()) {
            if (p->name == who) {
                guidePlayers << who;
            }
        }
    }
    rows.push_back({ "Horn Part Guides", FOLDER_BLURB["Horn Part Guides"],
                     guidePlayers.isEmpty() ? QString("<i>none yet</i>") : esc(guidePlayers.join(", ")) });
    if (!d.updateNotes.isEmpty()) {
        rows.push_back({ d.updateNotes, FOLDER_BLURB["Update Notes"], "one per band member" });
    }
    if (d.archive) {
        rows.push_back({ "Version History", FOLDER_BLURB["Version History"], plural(d.archive, "file") });
    }

    b += "<h2>What is in here</h2><table><tr><th style=\"width:22%\">Folder</th><th>What it is</th>"
         "<th style=\"width:27%\">Contents</th></tr>";
    for (const Row& r : rows) {
        b += QString("<tr><td class=\"fold mono\">%1</td><td>%2</td><td class=\"dim\">%3</td></tr>").arg(esc(r.folder), r.what, r.contents);
    }
    b += "</table>";
    if (d.hasRecordingsPdf) {
        b += QString("<div class=\"dim\" style=\"font-size:8.8pt;margin-top:4px;\"><b>%1 - Recordings.pdf</b>, next to this "
                     "file, links to every recording of the tune.</div>").arg(code);
    }

    if (real) {
        b += "<h2>Horn arrangements in detail</h2>";
        for (const ArrangementInfo& a : d.arrangements) {
            const QString ins = a.solo ? QString("The whole tune on one horn, one sheet per instrument.")
                                : a.generic ? QString("Any instruments &mdash; parts are provided for each chair in the keys "
                                                      "and clefs of the horns that can sit in it, so play whichever matches your horn.")
                                : esc(a.instruments.join(", "));
            b += QString("<div class=\"arr\"><div class=\"nm\">%1</div><div class=\"ins\"><b>%2.</b> %3</div>"
                         "<div class=\"fl\">Sheets: %4</div></div>")
                 .arg(esc(a.folder), a.solo ? QString("1 horn") : QString("%1 horns").arg(a.n), ins, names(a.files));
        }
    }

    if (!d.issues.empty()) {
        b += "<h2>Sheets that exist but are not finished</h2><p style=\"margin:2px 0 6px;font-size:9.5pt;color:#3d4356;\">"
             "These files open fine but have little or no music in them. Do not assume a part is playable just because "
             "the PDF is there.</p><table><tr><th style=\"width:30%\">Where</th><th style=\"width:34%\">Sheet</th><th>Problem</th></tr>";
        int shown = 0;
        for (const SheetIssue& i : d.issues) {
            if (++shown > 24) {
                break;
            }
            const bool blank = i.quality == Quality::Empty;
            b += QString("<tr><td class=\"fold mono\" style=\"font-size:8.6pt;\">%1</td><td>%2</td><td style=\"color:%3;\">%4</td></tr>")
                 .arg(esc(i.folder), esc(stripCode(i.file, code)), blank ? red : gold,
                      blank ? QString("Staves and form are laid out, but no music has been written yet.")
                      : QString("Runs fewer pages than the rest of the tune &mdash; may not be finished."));
        }
        b += "</table>";
        if (d.issues.size() > 24) {
            b += QString("<div class=\"dim\" style=\"font-size:8.8pt;\">&hellip; and %1 more.</div>").arg(d.issues.size() - 24);
        }
    }

    QStringList missing;
    if (d.status.lead != "done") {
        missing << "a usable lead sheet";
    }
    if (d.status.rhythm != "done") {
        missing << "a complete rhythm section chart";
    }
    if (d.status.three != "done") {
        missing << "a 3-horn arrangement";
    }
    if (d.status.horns.at(1) == "none") {
        missing << "1-horn versions";
    }
    if (!missing.isEmpty()) {
        b += QString("<div class=\"note\"><b>Still to be written:</b> %1. Ask Joel before a gig if you need one of these.</div>")
             .arg(esc(missing.join(", ")));
    }
    b += QString("<div class=\"foot\"><span>Starsign &middot; Sheets and Demos</span><span>%1 &middot; made %2 &middot; "
                 "this PDF is auto-generated, do not edit it by hand</span></div>")
         .arg(plural(d.currentSheets(), "current sheet"), prettyDate(today.toString(Qt::ISODate)));
    return htmlPage(d.name + " — What's Here", b);
}
}
