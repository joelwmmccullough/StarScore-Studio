/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: the band guide and Joel's progress tracker at the top of Sheets and Demos
 * (the old gen_home.py, checklist.py, progress_charts.py and playcounts.py).
 *
 * Play counts come from setlist.fm's stats pages: this year plus last year (Joel's choice, Oct 2026).
 */
#include "orghtml.h"

#include <algorithm>
#include <set>

#include <QRegularExpression>

namespace mu::project::starscore::org {
int PlayCounts::plays(const QString& code) const
{
    auto a = thisYear.find(code);
    auto b = lastYear.find(code);
    return (a == thisYear.end() ? 0 : a->second) + (b == lastYear.end() ? 0 : b->second);
}

static const QMap<QString, int> CAT_ORDER { { "orig", 1 }, { "cover", 2 }, { "vocal", 3 }, { "wip", 4 } };
static const QMap<QString, QString> CAT_LABEL { { "orig", "Starsign originals" }, { "cover", "Covers" },
    { "vocal", "Covers that need a vocalist" }, { "wip", "Works in progress" } };
static const QMap<QString, QString> FULL_TITLE { { "Live Strong + Strasbourg", "Live Strong / Strasbourg / St. Denis (medley)" } };

static std::vector<const SongInfo*> ordered(const Library& lib)
{
    std::vector<const SongInfo*> out;
    for (const SongInfo& s : lib.songs) {
        out.push_back(&s);
    }
    std::stable_sort(out.begin(), out.end(), [](const SongInfo* a, const SongInfo* b) {
        const int ca = CAT_ORDER.value(a->cat, 9), cb = CAT_ORDER.value(b->cat, 9);
        return ca != cb ? ca < cb : a->name < b->name;
    });
    return out;
}

static bool realArrangement(const ArrangementInfo& a)
{
    return !a.scoreOnly && a.usable && a.emptyParts.isEmpty();
}

static const char* HOME_CSS = R"CSS(
.chart{margin:10px 0 4px;break-inside:avoid;}
.chart h3{font-size:11pt;color:#16213e;margin:0 0 1px;}
.chart .cap{font-size:8.6pt;color:#6d7387;margin:0 0 7px;line-height:1.45;}
.chlg{display:flex;gap:14px;flex-wrap:wrap;margin:0 0 7px;font-size:8.6pt;color:#3d4356;}
.chlg span{display:inline-flex;align-items:center;gap:5px;}
.chlg i{width:10px;height:10px;border-radius:3px;display:inline-block;}
h2{margin:14px 0 6px;}
.hero{background:#16213e;color:#fff;margin:0 0 16px;padding:22px 26px 20px;border-radius:8px;}
.hero h1{color:#fff;font-size:27pt;margin:0;}
.hero .sub{color:#a9b4d0;font-size:10.5pt;margin-top:5px;}
.tiles{display:flex;gap:11px;margin:13px 0 4px;}
.tile .big{font-size:26pt;}
.map{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:9.6pt;background:#f7f8fb;
     border:1px solid #e3e7f0;border-radius:6px;padding:10px 14px;line-height:1.55;}
.map b{color:#16213e;} .map .c{color:#8b90a0;}
.step{display:flex;gap:11px;margin:9px 0;align-items:flex-start;}
.step .n{flex:0 0 24px;height:24px;border-radius:50%;background:#16213e;color:#fff;
   font-weight:700;font-size:10pt;text-align:center;line-height:24px;}
.keytab td{padding:3px 8px 3px 0;font-size:9.5pt;border-bottom:1px solid #f0f2f7;}
.keytab .ab{font-family:'SF Mono',Menlo,Consolas,monospace;font-weight:700;color:#16213e;width:42px;}
.sq{display:inline-block;width:11px;height:11px;border-radius:2.5px;}
.ladder td{font-size:9.4pt;padding:3.5px 8px 3.5px 0;border-bottom:1px solid #f0f2f7;}
.ladder .n{font-family:'SF Mono',Menlo,Consolas,monospace;font-weight:700;color:#16213e;width:34px;}
.idx td{font-size:9.1pt;padding:3px 6px 3px 0;border-bottom:1px solid #f2f4f8;}
.idx .cd{font-family:'SF Mono',Menlo,Consolas,monospace;font-weight:700;color:#16213e;width:34px;}
.grp{background:#eef1f7 !important;font-weight:700;color:#16213e;font-size:8.6pt;
     text-transform:uppercase;letter-spacing:.8px;padding:5px 6px !important;}
.cols{display:grid;grid-template-columns:1fr 1fr;column-gap:20px;}
.tk{display:flex;align-items:baseline;gap:5px;font-size:8.5pt;line-height:1.28;
    padding:1.7px 0;border-bottom:1px solid #f2f4f8;break-inside:avoid;min-width:0;}
.tk .cb{flex:0 0 8px;height:8px;border:1.2px solid #9aa1b4;border-radius:2px;display:inline-block;position:relative;top:1px;}
.tk .no{flex:0 0 20px;text-align:right;color:#a2a8b8;font-size:7.4pt;font-family:'SF Mono',Menlo,Consolas,monospace;}
.tk .nm{color:#16213e;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:44%;}
.tk .ac{color:#6d7387;flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.anat{margin:12px auto 2px;border-collapse:collapse;background:#f7f8fb;border:1px solid #e3e7f0;border-radius:6px;padding:6px;width:auto;}
.anat td{text-align:center;padding:3px 16px;font-family:'SF Mono',Menlo,Consolas,monospace;border:none;}
.anat .k1{font-size:13pt;font-weight:700;color:#16213e;padding-top:14px;}
.anat .ar{color:#a2a8b8;font-size:10pt;padding:1px 16px;}
.anat .k2{font-size:9.4pt;color:#3d4356;padding-bottom:14px;}
.tk .pc{flex:0 0 28px;text-align:right;color:#a2a8b8;font-size:7.4pt;font-family:'SF Mono',Menlo,Consolas,monospace;}
.tk.hot .nm{color:#8a2f18;} .tk.hot .cb{border-color:#c07a5e;border-width:1.4px;}
.tierhd{margin:15px 0 5px;padding-bottom:3px;border-bottom:2px solid #16213e;
    display:flex;justify-content:space-between;align-items:baseline;break-after:avoid;}
.tierhd .t{font-size:12pt;font-weight:700;color:#16213e;}
.tierhd .c{font-size:8.4pt;color:#6d7387;text-transform:uppercase;letter-spacing:.7px;}
.tierbl{font-size:8.9pt;color:#6d7387;margin:0 0 6px;break-after:avoid;}
.callout{background:#16213e;color:#fff;border-radius:8px;padding:14px 18px;margin:0 0 14px;}
.callout .lab{font-size:8pt;text-transform:uppercase;letter-spacing:1px;color:#93a0c4;}
.callout .big2{font-size:14.5pt;font-weight:700;margin-top:3px;line-height:1.25;}
.callout .sm{font-size:8.8pt;color:#c3cbe2;margin-top:5px;line-height:1.4;}
)CSS";

// ------------------------------------------------------------------ band guide
QString bandGuideHtml(const Library& lib, const QDate& today)
{
    const auto songs = ordered(lib);
    int nOrig = 0, nCov = 0, nArr = 0;
    for (const SongInfo* d : songs) {
        nOrig += d->cat == "orig";
        nCov += d->cat == "cover" || d->cat == "vocal";
        for (const ArrangementInfo& a : d->arrangements) {
            nArr += realArrangement(a);
        }
    }
    QString b;
    auto foot = [&](const QString& label, bool last = false) {
        b += QString("<div class=\"foot\"><span>Starsign &middot; Band Guide</span><span>%1%2</span></div>")
             .arg(label, last ? QString(" &middot; made %1 &middot; this PDF is auto-generated, do not edit it by hand")
                  .arg(prettyDate(today.toString(Qt::ISODate))) : QString());
        if (!last) {
            b += "<div class=\"pagebreak\"></div>";
        }
    };

    b += "<div class=\"hero\"><h1>Starsign &mdash; Sheets and Demos</h1><div class=\"sub\">How this drive is laid out, and how "
         "to find your part in about ten seconds.</div></div>";
    b += QString("<p style=\"margin:10px 0 0;color:#6d7387;font-size:9.6pt;\"><b style=\"color:#16213e;\">%1</b> originals &nbsp;&middot;&nbsp; "
                 "<b style=\"color:#16213e;\">%2</b> covers &nbsp;&middot;&nbsp; <b style=\"color:#16213e;\">%3</b> horn arrangements written</p>")
         .arg(nOrig).arg(nCov).arg(nArr);
    b += "<h2>The top level</h2><div class=\"map\">"
         "<b>1 &hellip;</b> &nbsp;Starsign originals &nbsp;<span class=\"c\">(one folder per tune, A&ndash;Z)</span><br>"
         "<b>2 &hellip;</b> &nbsp;Covers<br>"
         "<b>3 &hellip;</b> &nbsp;Covers that need a vocalist &nbsp;<span class=\"c\">(no singer, so mostly parked)</span><br>"
         "<b>4 Works In Progress</b> &nbsp;<span class=\"c\">(not gig-ready &mdash; do not learn these yet)</span><br>"
         "<b>5 Archive</b> &nbsp;<span class=\"c\">(old versions of everything, kept forever)</span><br>"
         "<b>6 Inbox</b> &nbsp;<span class=\"c\">(drop new sheets here &mdash; Joel&rsquo;s next update files them)</span>"
         "</div><div class=\"note\">The number is only there to force the sort order. Ignore it when you are searching &mdash; "
         "typing the song name into Drive search still finds it instantly.</div>";
    b += "<h2>Finding your part</h2>"
         "<div class=\"step\"><div class=\"n\">1</div><div>Open the song folder.</div></div>"
         "<div class=\"step\"><div class=\"n\">2</div><div>Open the <b>What&rsquo;s Here</b> PDF at the bottom of that folder. "
         "It lists every sheet in the tune, which instruments each arrangement needs, and which sheets are still blank.</div></div>"
         "<div class=\"step\"><div class=\"n\">3</div><div>Go to the folder for your section &mdash; <b>1 Lead Sheet</b>, "
         "<b>1 Rhythm</b>, or the <b>nH</b> horn arrangement matching how many horns are on the gig.</div></div>";
    b += "<h2>Every song folder looks like this</h2><div class=\"map\">"
         "<b>1 Amplitudes</b><br>"
         "&nbsp;&nbsp;<b>1 Lead Sheet</b><br>"
         "&nbsp;&nbsp;<b>1 Rhythm</b><br>"
         "&nbsp;&nbsp;<b>1H</b> &nbsp;<span class=\"c\">the whole tune on one horn</span><br>"
         "&nbsp;&nbsp;<b>3H Tpt Alt Ten</b> &nbsp;<span class=\"c\">&larr; a horn arrangement</span><br>"
         "&nbsp;&nbsp;<b>Demos</b> &nbsp;<span class=\"c\">audio</span> &nbsp;&nbsp;<b>Extras</b> &nbsp;<span class=\"c\">strings, guests, odds and ends</span><br>"
         "&nbsp;&nbsp;<b>Horn Part Guides</b> &nbsp;<span class=\"c\">what differs between your parts</span><br>"
         "&nbsp;&nbsp;<b>Reference PDFs</b> &nbsp;<span class=\"c\">transcriptions and notes, on some tunes</span><br>"
         "&nbsp;&nbsp;<b>Update Notes yy-mm-dd</b> &nbsp;<span class=\"c\">what changed, per player</span><br>"
         "&nbsp;&nbsp;<b>Version History</b> &nbsp;<span class=\"c\">the archive &mdash; never play from here</span><br>"
         "&nbsp;&nbsp;<span class=\"c\">AMPL - Recordings.pdf &nbsp;&middot;&nbsp; AMPL - What&rsquo;s Here.pdf</span>"
         "</div><div class=\"note\"><b>A PDF being there does not mean the part is written.</b> Some sheets are still blank "
         "templates &mdash; form and rehearsal marks, no notes. Each song&rsquo;s <b>What&rsquo;s Here</b> PDF says which.</div>";
    foot("How the drive is laid out");

    b += "<h1>Reading the horn folder names</h1><div class=\"sub\">This is the only thing in the drive you have to learn. It takes a minute.</div>"
         "<table class=\"anat\"><tr><td class=\"k1\">3H</td><td class=\"k1\">Tpt</td><td class=\"k1\">Alt</td><td class=\"k1\">Ten</td></tr>"
         "<tr><td class=\"ar\">&darr;</td><td class=\"ar\">&darr;</td><td class=\"ar\">&darr;</td><td class=\"ar\">&darr;</td></tr>"
         "<tr><td class=\"k2\">3 horns</td><td class=\"k2\">trumpet</td><td class=\"k2\">alto sax</td><td class=\"k2\">tenor sax</td></tr></table>"
         "<p style=\"margin:11px 0 2px;\">The number before <b>H</b> is how many horn players the arrangement is written for. Everything "
         "after it lists the instruments in score order, highest to lowest. A number in front of an instrument means more than one of "
         "them: <b>2Tpt</b> is two trumpet parts, <b>2Ten</b> is two tenor parts.</p>";
    // the one horn table (orglibrary), written the guide's way: "Soprano sax", "Bass trombone"
    const auto& horns = hornOrder();
    const int half = (int(horns.size()) + 1) / 2;
    b += "<h2>The instrument codes</h2><table><tr><td style=\"width:50%;vertical-align:top;padding:0;\"><table class=\"keytab\">";
    for (int i = 0; i < int(horns.size()); ++i) {
        if (i == half) {
            b += "</table></td><td style=\"vertical-align:top;padding:0;\"><table class=\"keytab\">";
        }
        const QString name = horns[i].first.left(1) + horns[i].first.mid(1).toLower();
        b += QString("<tr><td class=\"ab\">%1</td><td>%2</td></tr>").arg(horns[i].second, name);
    }
    b += "</table></td></tr></table>";
    b += "<h2>The standard ladder</h2><p style=\"margin:2px 0 4px;\">Unless a tune says otherwise, arrangements are built up in this order:</p>"
         "<table class=\"ladder\">"
         "<tr><td class=\"n\">2H</td><td>Trumpet, Tenor sax</td></tr>"
         "<tr><td class=\"n\">3H</td><td>Trumpet, Alto sax, Tenor sax</td></tr>"
         "<tr><td class=\"n\">4H</td><td>Trumpet, Alto sax, Tenor sax, Trombone</td></tr>"
         "<tr><td class=\"n\">5H</td><td>Trumpet 1 &amp; 2, Alto sax, Tenor sax, Trombone</td></tr>"
         "<tr><td class=\"n\">6H</td><td>Trumpet 1 &amp; 2, Alto sax, Tenor sax, Soprano sax, Trombone</td></tr>"
         "<tr><td class=\"n\">7H</td><td>Trumpet 1 &amp; 2, Alto sax, Tenor sax, Soprano sax, Trombone, Bass trombone</td></tr></table>"
         "<div class=\"note\"><b>2H is fixed</b> &mdash; always trumpet and tenor.<br><b>3H, 4H and 5H</b> &mdash; the alto is the "
         "seat that sometimes gets swapped for clarinet, flute or something else; the other horns stay put.<br><b>6H and 7H</b> &mdash; "
         "soprano sax is the standard sixth horn (not a second tenor), and the soprano is the seat most likely to be swapped.<br>Either "
         "way, <b>trust the folder name over this ladder</b> &mdash; it spells out what the chart actually calls for.</div>";
    int oneHorn = 0;
    for (const SongInfo* d : songs) {
        oneHorn += d->status.horns.at(1) == "done";
    }
    b += QString("<h2>1H &mdash; one-horn versions</h2><p style=\"margin:2px 0 0;\">A <b>1H</b> folder holds the whole tune on a "
                 "single horn part, one sheet per instrument (trumpet, alto, tenor, trombone&hellip;) &mdash; for gigs where only one "
                 "horn shows up, or for practising the tune on your own. %1</p>")
         .arg(oneHorn ? QString("%1 so far.").arg(plural(oneHorn, "tune has one", "tunes have one"))
              : QString("None are written yet."));
    foot("Reading the horn folder names");

    b += "<h1>Flexible arrangements</h1><p style=\"margin-top:10px;\">Folders like <b>3H Flexible</b> are not written "
         "for named instruments &mdash; they are written for <b>chairs</b>. Find the chair your instrument covers below, then open that "
         "horn number for your instrument (<span class=\"mono\">Horn 2 - Alto Sax</span>) or, on older tunes, in your transposition "
         "(<b>in B&#9837;</b>, <b>in E&#9837;</b>, <b>in C</b>, <b>bass clef</b> or <b>alto clef</b>).</p>";
    auto chairtab = [](const QString& title, const std::vector<QStringList>& rows) {
        QString h = "<h2>" + title + "</h2><table class=\"mtx\"><tr><th class=\"c\" style=\"width:44px\">Horn</th><th>Instruments that "
                    "cover this chair</th><th class=\"c\" style=\"width:84px\">Amateur range</th><th class=\"c\" style=\"width:84px\">Pro range</th></tr>";
        for (const QStringList& r : rows) {
            h += QString("<tr><td class=\"c mono\" style=\"font-weight:700;color:#16213e;\">%1</td><td>%2</td>"
                         "<td class=\"c mono\" style=\"font-size:9pt;\">%3</td><td class=\"c mono\" style=\"font-size:9pt;\">%4</td></tr>")
                 .arg(r[0], r[1], r[2], r[3]);
        }
        return h + "</table>";
    };
    b += chairtab("2-Horn Arrangement", {
        { "1", "Soprano sax, clarinet, trumpet, alto sax, violin", "G&#9839;3&ndash;G&#9839;5", "E3&ndash;C&#9839;6" },
        { "2", "Tenor sax, bari sax, trombone, bass clarinet, cello", "G&#9839;2&ndash;B4", "G&#9839;2&ndash;D5" } });
    b += chairtab("3-Horn Arrangement", {
        { "1", "Clarinet, soprano sax, trumpet, alto sax, violin &mdash; a flute variation is also provided", "G&#9839;3&ndash;G&#9839;5",
          "E3&ndash;C&#9839;6" },
        { "2", "Trumpet, clarinet, alto sax, tenor sax, viola", "E3&ndash;D&#9839;5", "E3&ndash;C&#9839;6" },
        { "3", "Tenor sax, bari sax, trombone, bass clarinet, cello", "G&#9839;2&ndash;B4", "G&#9839;2&ndash;D5" } });
    b += "<div class=\"note\">Horn 1 is the top line. The two ranges cover the same music: the wider <b>pro range</b> is what the part "
         "actually asks for, the <b>amateur range</b> is the narrower span you can get away with when the extremes are out of reach. "
         "Pick the chair that suits the player, not just the instrument &mdash; an alto can sit in chair 1 or chair 2 of the 3-horn.</div>";
    foot("Flexible arrangements");

    b += "<h1>Song index</h1><div class=\"sub\">Every file inside a song folder starts with that song&rsquo;s four-letter code, so "
         "<span class=\"mono\">AMPL - Trumpet.pdf</span> is the trumpet part for Amplitudes.</div>"
         "<table class=\"idx\"><tr><th style=\"width:40px\">Code</th><th>Song</th><th style=\"width:52px\">Lead</th>"
         "<th style=\"width:58px\">Rhythm</th><th>Horn arrangements</th><th style=\"width:56px\">Blank</th></tr>";
    QString lastCat;
    for (const SongInfo* d : songs) {
        if (d->cat != lastCat) {
            lastCat = d->cat;
            b += QString("<tr><td class=\"grp\" colspan=\"6\">%1</td></tr>").arg(CAT_LABEL.value(lastCat));
        }
        QStringList av;
        for (const ArrangementInfo& a : d->arrangements) {
            if (realArrangement(a)) {
                av << QString("%1H").arg(a.n);
            }
        }
        av.removeDuplicates();
        int nblank = 0;
        for (const SheetIssue& i : d->issues) {
            nblank += i.quality == Quality::Empty;
        }
        const QString lead = d->status.lead == "done" ? QString("yes")
                             : d->status.lead == "partial" ? QString("<span style=\"color:#a8391f;\">blank</span>")
                             : QString("<span class=\"dim\">&mdash;</span>");
        b += QString("<tr><td class=\"cd\">%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
             .arg(d->code, esc(FULL_TITLE.value(d->name, d->name)), lead,
                  d->rhythm.isEmpty() ? QString("<span class=\"dim\">&mdash;</span>") : QString("%1 parts").arg(d->rhythm.size()),
                  av.isEmpty() ? QString("<span class=\"dim\">none yet</span>") : av.join(", "),
                  nblank ? QString("<span style=\"color:#a8391f;\">%1</span>").arg(nblank) : QString("<span class=\"dim\">&mdash;</span>"));
    }
    b += "</table>";
    foot("Song index");

    b += "<h1>The rest of it</h1>"
         "<h2>Extras</h2><p>Anything that is neither a horn nor a rhythm part: violin, cello and combined <b>Strings</b> scores, parts "
         "written for guest players who sat in, alternate takes on a part, and the occasional full score. Worth a look if you cannot find "
         "yourself in Rhythm or a horn folder.</p>"
         "<h2>Version History</h2><p>Every song folder has one, and there is a big <b>5 Archive</b> at the top level too. Old sheets are "
         "never deleted here &mdash; they are moved. Two things to know:</p><ul><li>Archived files keep their <b>original names</b>, "
         "including the old convention of naming a part after its player. That is deliberate: it is "
         "the record of who played what.</li><li><b>5 Archive / Old 6-Horn Arrangements</b> is a complete snapshot of the library as it "
         "stood before the current arrangements. History, not current music.</li></ul>"
         "<h2>Works In Progress</h2><p>Tunes still being written or reworked. They are kept out of the main list on purpose &mdash; do "
         "not spend practice time on them until Joel moves them up.</p>"
         "<h2>The PDFs</h2><table><tr><th style=\"width:44%\">File</th><th>What it is</th></tr>"
         "<tr><td class=\"mono\">Starsign Band Guide.pdf</td><td>This document. Lives at the top level.</td></tr>"
         "<tr><td class=\"mono\">XXXX - What&rsquo;s Here.pdf</td><td>One inside every song folder. Every sheet in that tune, the exact "
         "instrumentation of each arrangement, and which sheets are still blank.</td></tr>"
         "<tr><td class=\"mono\">XXXX - Recordings.pdf</td><td>One inside every song folder. Links to every album take, live session "
         "and filmed show of the tune.</td></tr>"
         "<tr><td class=\"mono\">Horn Part Guides/XXXX - &lt;you&gt;.pdf</td><td>For each horn player: where your parts in the 1-, 2- "
         "and 3-horn charts are the same and where they differ.</td></tr>"
         "<tr><td class=\"mono\">Update Notes &hellip;/XXXX - Changelog - &lt;you&gt;.pdf</td><td>What changed on your sheets, newest "
         "first. The folder&rsquo;s date is the last time the tune changed.</td></tr>"
         "<tr><td class=\"mono\">Starsign Progress - Joel.pdf</td><td>Joel&rsquo;s writing tracker &mdash; handy if you want to know why "
         "a part does not exist yet.</td></tr>"
         "<tr><td class=\"mono\">Starsign Maintenance Report.pdf</td><td>The log of every update to this drive.</td></tr></table>"
         "<div class=\"note\">A few older parts could not be matched to an instrument automatically and are named after the player "
         "instead (&ldquo;CODE - <i>name</i> (unlabeled).pdf&rdquo;). If you recognise one, tell Joel and it will get a "
         "proper name.</div>";
    foot("Everything else", true);
    return htmlPage("Starsign Band Guide", b, HOME_CSS);
}

// ------------------------------------------------------------------ checklist (checklist.py)
static const std::map<QString, QStringList> RHYTHM_ALIASES {
    { "bass", { "Bass", "Bass Synth" } }, { "gtr", { "Guitar" } }, { "keys", { "Keys", "Elec Piano", "Organ", "Clavinet" } },
    { "drums", { "Drums" } }, { "perc", { "Percussion", "Congas" } },
};
static const QMap<QString, QString> RHYTHM_LABEL { { "bass", "bass" }, { "gtr", "guitar" }, { "keys", "keys" }, { "drums", "drums" },
    { "perc", "percussion" } };

static QString rhythmState(const SongInfo& d, const QString& key)
{
    static const QRegularExpression paren(" \\(.*\\)$");
    int best = -1;
    for (const QString& part : d.rhythm) {
        if (RHYTHM_ALIASES.at(key).contains(QString(part).remove(paren))) {
            const Quality q = d.rhythmq.count(part) ? d.rhythmq.at(part) : Quality::Ok;
            const int rank = q == Quality::Ok ? 0 : q == Quality::Thin ? 1 : 2;
            if (best < 0 || rank < best) {
                best = rank;
            }
        }
    }
    return best < 0 ? "none" : best == 0 ? "done" : best == 1 ? "thin" : "blank";
}

static QString hornState(const SongInfo& d, int n)
{
    bool any = false, score = false, blank = false;
    for (const ArrangementInfo& a : d.arrangements) {
        if (a.n != n) {
            continue;
        }
        any = true;
        if (realArrangement(a)) {
            return "done";
        }
        score |= a.scoreOnly;
        blank |= !a.emptyParts.isEmpty();
    }
    return !any ? "none" : score ? "score" : blank ? "blank" : "none";
}

static const std::vector<std::array<QString, 3> > TIERS {
    { "3h", "3-horn arrangements", "Trumpet / alto / tenor, occasionally swapping the alto for something else. The whole point of the "
      "exercise &mdash; nothing else matters until these exist. Tunes with no 4-horn arrangement either come first: there is nothing "
      "at all to play them from, whereas a 4-horn chart can be cut down at a pinch." },
    { "lead", "Lead sheets", "The one sheet everybody can read from." },
    { "bass", "Bass charts", "" },
    { "gtr", "Guitar charts", "" },
    { "2h", "2-horn arrangements", "Trumpet / tenor." },
    { "4h", "4-horn arrangements", "Trumpet / alto / tenor / trombone." },
    { "1h", "1-horn versions", "Whole tune on a single horn, one sheet per instrument." },
    { "rest", "Everything else", "5-, 6- and 7-horn arrangements plus keys, drums and percussion. Priority between these is nebulous "
      "&mdash; they are ordered by how often the tune gets played, so work top-down or follow your nose." },
};

std::vector<Task> buildTasks(const Library& lib, const PlayCounts& plays)
{
    static const QMap<QString, QString> SHORT { { "none", "write it" }, { "score", "extract parts &mdash; score exists" },
        { "blank", "fill in the blank parts" }, { "thin", "finish it &mdash; runs short" } };
    static const QMap<QString, QString> HORN_VERB { { "none", "Write the %1-horn arrangement" },
        { "score", "Extract the %1-horn parts &mdash; the score is already written" }, { "blank", "Fill in the blank %1-horn parts" } };
    static const QMap<QString, QString> RHY_VERB { { "none", "Write the %1 chart" },
        { "blank", "Fill in the blank %1 chart &mdash; the file exists but has no notes" },
        { "thin", "Finish the %1 chart &mdash; it runs short of the rest of the tune" } };
    std::vector<Task> tasks;
    for (const SongInfo& d : lib.songs) {
        auto add = [&](const QString& tier, const QString& text, const QString& shortText, bool first = false) {
            Task t;
            t.tier = tier;
            t.song = d.name;
            t.code = d.code;
            t.text = text;
            t.shortText = shortText;
            t.thisYear = plays.thisYear.count(d.code) ? plays.thisYear.at(d.code) : 0;
            t.lastYear = plays.lastYear.count(d.code) ? plays.lastYear.at(d.code) : 0;
            t.sc = t.thisYear + t.lastYear;
            t.wip = d.cat == "wip";
            t.sub = first ? 0 : 1;
            tasks.push_back(t);
        };
        const QString st3 = hornState(d, 3);
        if (st3 != "done") {
            add("3h", HORN_VERB.value(st3).arg(3), SHORT.value(st3), hornState(d, 4) != "done");
        }
        if (d.status.lead == "none") {
            add("lead", "Write the lead sheet", "write it");
        } else if (d.status.lead == "partial") {
            add("lead", "Fill in the lead sheet &mdash; it is currently a blank form template", "blank template &mdash; fill it in");
        } else {
            for (const SheetIssue& i : d.issues) {
                if (i.folder == "1 Lead Sheet" && i.quality == Quality::Thin) {
                    add("lead", "Check the lead sheet &mdash; it runs short of the rest of the tune", "runs short &mdash; check it");
                }
            }
        }
        for (const QString& key : { QString("bass"), QString("gtr") }) {
            const QString st = rhythmState(d, key);
            if (st != "done") {
                add(key, RHY_VERB.value(st).arg(RHYTHM_LABEL.value(key)), SHORT.value(st));
            }
        }
        for (const auto& [n, tier] : std::vector<std::pair<int, QString> > { { 2, "2h" }, { 4, "4h" }, { 1, "1h" } }) {
            const QString st = hornState(d, n);
            if (st != "done") {
                add(tier, HORN_VERB.value(st).arg(n), SHORT.value(st));
            }
        }
        for (int n : { 5, 6, 7 }) {
            const QString st = hornState(d, n);
            if (st != "done") {
                add("rest", QString("%1-horn: ").arg(n) + SHORT.value(st), QString("%1-horn &mdash; ").arg(n) + SHORT.value(st));
            }
        }
        for (const QString& key : { QString("keys"), QString("drums"), QString("perc") }) {
            const QString st = rhythmState(d, key);
            if (st != "done") {
                add("rest", RHY_VERB.value(st).arg(RHYTHM_LABEL.value(key)), RHYTHM_LABEL.value(key) + " &mdash; " + SHORT.value(st));
            }
        }
    }
    std::map<QString, int> order;
    for (size_t i = 0; i < TIERS.size(); ++i) {
        order[TIERS[i][0]] = int(i);
    }
    std::stable_sort(tasks.begin(), tasks.end(), [&](const Task& a, const Task& b) {
        if (order[a.tier] != order[b.tier]) {
            return order[a.tier] < order[b.tier];
        }
        if (a.sub != b.sub) {
            return a.sub < b.sub;
        }
        if (a.sc != b.sc) {
            return a.sc > b.sc;
        }
        return a.song != b.song ? a.song < b.song : a.text < b.text;
    });
    for (size_t i = 0; i < tasks.size(); ++i) {
        tasks[i].n = int(i) + 1;
    }
    return tasks;
}

// ------------------------------------------------------------------ charts (progress_charts.py)
static const QString C_DONE = "#2f7d5c", C_PART = "#c8a24a", C_NONE = "#8d95a8", C_LEAD = "#2a78d6", C_3H = "#1baf7a";
static const QString INK = "#1b2030", MUTED = "#6d7387", GRID = "#eef0f5", AXIS = "#c9cedb";
static const QString FONT = "'Helvetica Neue',Helvetica,Arial,sans-serif";

struct ChartRow {
    QString label;
    int done = 0, part = 0, none = 0;
};

static QString barChart(const std::vector<ChartRow>& rows, int width = 560)
{
    const int labW = 152, right = 46, rowH = 21, gap = 9;
    const int plot = width - labW - right;
    const int height = int(rows.size()) * (rowH + gap) + 4;
    QString out = QString("<svg viewBox=\"0 0 %1 %2\" width=\"100%\" height=\"%2\" style=\"font-family:%3;overflow:visible\">")
                  .arg(width).arg(height).arg(FONT);
    for (size_t i = 0; i < rows.size(); ++i) {
        const ChartRow& r = rows[i];
        const int y = int(i) * (rowH + gap);
        out += QString("<text x=\"%1\" y=\"%2\" text-anchor=\"end\" font-size=\"9.2\" fill=\"%3\">%4</text>")
               .arg(labW - 10).arg(y + rowH * 0.72).arg(INK, r.label);
        double x = labW;
        const int total = std::max(1, r.done + r.part + r.none);
        const std::vector<std::pair<int, QString> > segs { { r.done, C_DONE }, { r.part, C_PART }, { r.none, C_NONE } };
        int last = 0;
        for (int k = 0; k < 3; ++k) {
            if (segs[k].first) {
                last = k;
            }
        }
        for (int k = 0; k < 3; ++k) {
            const int v = segs[k].first;
            if (!v) {
                continue;
            }
            const double w = double(plot) * v / total;
            const double gw = std::max(w - 2, 1.0);
            out += QString("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"%5\" fill=\"%6\"/>")
                   .arg(x, 0, 'f', 1).arg(y).arg(gw, 0, 'f', 1).arg(rowH).arg(k == last ? 4 : 0).arg(segs[k].second);
            if (w > 22) {
                out += QString("<text x=\"%1\" y=\"%2\" text-anchor=\"middle\" font-size=\"9\" font-weight=\"700\" fill=\"#ffffff\">%3</text>")
                       .arg(x + gw / 2, 0, 'f', 1).arg(y + rowH * 0.68).arg(v);
            }
            x += w;
        }
        out += QString("<text x=\"%1\" y=\"%2\" font-size=\"9\" fill=\"%3\">of %4</text>")
               .arg(labW + plot + 8).arg(y + rowH * 0.72).arg(MUTED).arg(r.done + r.part + r.none);
    }
    return out + "</svg>";
}

static std::vector<int> monthly(const QStringList& dates, const QStringList& months)
{
    std::map<QString, int> counts;
    for (const QString& d : dates) {
        counts[d.left(7)]++;
    }
    std::vector<int> series;
    int run = 0;
    // anything before the first month counts from the start
    for (const auto& [m, c] : counts) {
        if (!months.isEmpty() && m < months.first()) {
            run += c;
        }
    }
    for (const QString& m : months) {
        run += counts.count(m) ? counts[m] : 0;
        series.push_back(run);
    }
    return series;
}

static QString lineChart(const QStringList& added, const QStringList& leads, int total, const QDate& today, int width = 560, int height = 205)
{
    QStringList months;
    QString start = "2022-01";
    for (const QString& d : added) {
        if (d.left(7) < start) {
            start = d.left(7);
        }
    }
    QDate m = QDate::fromString(start + "-01", Qt::ISODate);
    while (m <= today) {
        months << m.toString("yyyy-MM");
        m = m.addMonths(1);
    }
    const std::vector<std::pair<QString, std::pair<std::vector<int>, QString> > > data {
        { "Tunes in the book", { monthly(added, months), C_LEAD } }, { "Lead sheets written", { monthly(leads, months), C_3H } } };
    const int L = 30, R = 118, T = 12, B = 26;
    const int pw = width - L - R, ph = height - T - B;
    int top = total;
    for (const auto& d : data) {
        for (int v : d.second.first) {
            top = std::max(top, v);
        }
    }
    top = std::max(top, 1);
    auto X = [&](int i) { return L + double(pw) * i / std::max<int>(int(months.size()) - 1, 1); };
    auto Y = [&](int v) { return T + ph * (1 - double(v) / top); };
    QString out = QString("<svg viewBox=\"0 0 %1 %2\" width=\"100%\" height=\"%2\" style=\"font-family:%3;overflow:visible\">")
                  .arg(width).arg(height).arg(FONT);
    for (int v = 0; v <= top; v += 15) {
        out += QString("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"%4\" stroke-width=\"1\"/>"
                       "<text x=\"%5\" y=\"%6\" text-anchor=\"end\" font-size=\"8\" fill=\"%7\">%8</text>")
               .arg(L).arg(Y(v), 0, 'f', 1).arg(L + pw).arg(GRID).arg(L - 6).arg(Y(v) + 3, 0, 'f', 1).arg(MUTED).arg(v);
    }
    out += QString("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"%4\" stroke-width=\"1\"/>").arg(L).arg(T + ph).arg(L + pw).arg(AXIS);
    for (int i = 0; i < months.size(); ++i) {
        if (months[i].endsWith("-01")) {
            out += QString("<text x=\"%1\" y=\"%2\" text-anchor=\"middle\" font-size=\"8\" fill=\"%3\">%4</text>")
                   .arg(X(i), 0, 'f', 1).arg(height - 9).arg(MUTED, months[i].left(4));
        }
    }
    for (const auto& [name, d] : data) {
        const auto& vals = d.first;
        if (vals.empty()) {
            continue;
        }
        QStringList pts;
        for (size_t i = 0; i < vals.size(); ++i) {
            pts << QString("%1,%2").arg(X(int(i)), 0, 'f', 1).arg(Y(vals[i]), 0, 'f', 1);
        }
        out += QString("<polyline points=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"2\" stroke-linejoin=\"round\" stroke-linecap=\"round\"/>")
               .arg(pts.join(' '), d.second);
        const double ex = X(int(vals.size()) - 1), ey = Y(vals.back());
        out += QString("<circle cx=\"%1\" cy=\"%2\" r=\"4\" fill=\"%3\" stroke=\"#ffffff\" stroke-width=\"2\"/>"
                       "<text x=\"%4\" y=\"%5\" font-size=\"8.6\" fill=\"%6\">%7 <tspan font-weight=\"700\">%8</tspan></text>")
               .arg(ex, 0, 'f', 1).arg(ey, 0, 'f', 1).arg(d.second).arg(ex + 9, 0, 'f', 1).arg(ey + 3, 0, 'f', 1).arg(INK, name).arg(vals.back());
    }
    return out + "</svg>";
}

static QString legend(const std::vector<std::pair<QString, QString> >& items)
{
    QString b = "<div class=\"chlg\">";
    for (const auto& [label, colour] : items) {
        b += QString("<span><i style=\"background:%1\"></i>%2</span>").arg(colour, label);
    }
    return b + "</div>";
}

// ------------------------------------------------------------------ the tracker
static QString mark(const QString& st)
{
    return st == "done" ? "<span class=\"sq s-done\"></span>" : st == "partial" ? "<span class=\"sq s-part\"></span>"
           : "<span class=\"sq s-none\"></span>";
}

QString progressHtml(const Library& lib, const PlayCounts& plays, const std::vector<Task>& tasks, const QDate& today)
{
    const auto songs = ordered(lib);
    const int tot = std::max<int>(1, int(songs.size()));
    int p1 = 0, leadN = 0, rhN = 0, thN = 0;
    for (const SongInfo* d : songs) {
        p1 += d->phase1();
        leadN += d->status.lead == "done";
        rhN += d->status.rhythm == "done";
        thN += d->status.three == "done";
    }
    const int pct = int(std::lround(100.0 * p1 / tot));
    const int y = plays.year ? plays.year : today.year();

    QString b = "<div class=\"hero\"><h1>Progress tracker</h1><div class=\"sub\">Starsign &middot; Sheets and Demos &middot; for Joel</div></div>";
    if (!tasks.empty()) {
        const Task& t0 = tasks[0];
        QStringList rest;
        for (size_t i = 1; i < std::min<size_t>(5, tasks.size()); ++i) {
            rest << QString("%1 (%2)").arg(esc(tasks[i].song), tasks[i].shortText);
        }
        b += QString("<div class=\"callout\"><div class=\"lab\">Start here &mdash; task #1 of %1</div><div class=\"big2\">%2 &mdash; %3</div>"
                     "<div class=\"sm\">Played <b>%4</b> times in %5 and <b>%6</b> so far in %7 &mdash; the most-played tune that still needs "
                     "this. Full checklist at the back.</div><div class=\"sm\">Then: %8</div></div>")
             .arg(tasks.size()).arg(esc(t0.song), t0.text).arg(t0.lastYear).arg(y - 1).arg(t0.thisYear).arg(y).arg(rest.join(" &middot; "));
    }
    auto tile = [&](int n, const QString& lab) {
        return QString("<div class=\"tile\"><div class=\"big\">%1<span style=\"font-size:15pt;color:#8b90a0;\">/%2</span></div>"
                       "<div class=\"lab\">%3</div></div>").arg(n).arg(tot).arg(lab);
    };
    b += "<h2>Phase 1 &mdash; lead sheet + rhythm + 3&#8209;horn, every tune</h2><div class=\"tiles\">" + tile(p1, "Tunes fully done")
         + tile(leadN, "Lead sheets") + tile(rhN, "Rhythm charts") + tile(thN, "3-horn arrangements") + "</div>";
    b += QString("<div class=\"bar\"><i class=\"s-done\" style=\"width:%1%\"></i></div><div class=\"legend\"><b style=\"color:#16213e;\">%1% "
                 "of the library is Phase&nbsp;1 complete.</b> %2 tunes to go.</div>").arg(pct).arg(tot - p1);

    // chart 1
    auto hornCounts = [&](int n) {
        ChartRow r;
        for (const SongInfo* d : songs) {
            const QString st = hornState(*d, n);
            (st == "done" ? r.done : (st == "score" || st == "blank") ? r.part : r.none)++;
        }
        return r;
    };
    auto rhythmCounts = [&](const QStringList& keys) {
        ChartRow r;
        for (const SongInfo* d : songs) {
            for (const QString& k : keys) {
                const QString st = rhythmState(*d, k);
                (st == "done" ? r.done : (st == "thin" || st == "blank") ? r.part : r.none)++;
            }
        }
        return r;
    };
    std::vector<ChartRow> rows;
    auto named = [](ChartRow r, const QString& l) {
        r.label = l;
        return r;
    };
    rows.push_back(named(hornCounts(3), "3-horn arrangements"));
    ChartRow lead;
    for (const SongInfo* d : songs) {
        (d->status.lead == "done" ? lead.done : d->status.lead == "partial" ? lead.part : lead.none)++;
    }
    rows.push_back(named(lead, "Lead sheets"));
    rows.push_back(named(rhythmCounts({ "bass" }), "Bass charts"));
    rows.push_back(named(rhythmCounts({ "gtr" }), "Guitar charts"));
    rows.push_back(named(hornCounts(2), "2-horn arrangements"));
    rows.push_back(named(hornCounts(4), "4-horn arrangements"));
    rows.push_back(named(hornCounts(1), "1-horn versions"));
    ChartRow big;
    for (int n : { 5, 6, 7 }) {
        const ChartRow r = hornCounts(n);
        big.done += r.done;
        big.part += r.part;
        big.none += r.none;
    }
    rows.push_back(named(big, "5-, 6- and 7-horn"));
    rows.push_back(named(rhythmCounts({ "keys", "drums", "perc" }), "Keys, drums, percussion"));
    b += QString("<div class=\"chart\"><h3>Where every job stands</h3><p class=\"cap\">Each bar is one kind of job, scaled to its own total "
                 "so the proportions line up &mdash; the single-size rows count one job per tune across all %1, the last two count one per "
                 "arrangement size and per instrument. Read the top row first: it is the one that matters.</p>").arg(tot)
         + legend({ { "Done", C_DONE }, { "Started &mdash; blank, score-only or runs short", C_PART }, { "Not started", C_NONE } })
         + barChart(rows) + "</div>";

    // chart 2
    QStringList added, leads;
    for (const SongInfo* d : songs) {
        if (!d->oldestFileDate.isEmpty()) {
            added << d->oldestFileDate;
        }
        if (d->status.lead == "done" && !d->oldestLeadDate.isEmpty()) {
            leads << d->oldestLeadDate;
        }
    }
    if (!added.isEmpty()) {
        b += "<div class=\"chart\"><h3>How the book filled up</h3><p class=\"cap\">Dated from the files themselves &mdash; each tune "
             "counted from its oldest surviving file, each lead sheet from the oldest copy in that tune&rsquo;s Version History. The gap "
             "between the two lines is the backlog. Rhythm and 3-horn charts are not plotted: the old parts are filed per player "
             "(song name, then the player&rsquo;s name), so nothing in them says which arrangement they belong to, and a line "
             "drawn from that would be invention.</p>"
             + legend({ { "Tunes in the book", C_LEAD }, { "Lead sheets written", C_3H } }) + lineChart(added, leads, tot, today) + "</div>";
    }

    // nearest to finished
    auto gap = [](const SongInfo* d) {
        double g = 0;
        for (const QString& s : { d->status.lead, d->status.rhythm, d->status.three }) {
            g += s == "done" ? 0 : s == "partial" ? 0.5 : 1;
        }
        return g;
    };
    std::vector<const SongInfo*> todo;
    for (const SongInfo* d : songs) {
        if (gap(d) > 0) {
            todo.push_back(d);
        }
    }
    std::stable_sort(todo.begin(), todo.end(), [&](const SongInfo* a, const SongInfo* b) { return gap(a) < gap(b); });
    b += "<h2>What is left, nearest to finished first</h2><table class=\"mtx\"><tr><th style=\"width:34px\">Code</th><th>Song</th>"
         "<th class=\"c\" style=\"width:56px\">Lead</th><th class=\"c\" style=\"width:64px\">Rhythm</th><th class=\"c\" style=\"width:62px\">"
         "3-Horn</th><th style=\"width:40%\">Next job</th></tr>";
    for (const SongInfo* d : todo) {
        const SongStatus& s = d->status;
        QStringList need;
        if (s.lead == "partial") {
            need << "lead sheet (blank template exists)";
        } else if (s.lead == "none") {
            need << "lead sheet";
        }
        if (s.rhythm != "done") {
            QStringList miss;
            for (const auto& [role, v] : s.roles) {
                if (v != "done") {
                    miss << role + (v == "empty" ? " blank" : " missing");
                }
            }
            need << (miss.isEmpty() ? QString("rhythm") : "rhythm &mdash; " + miss.join(", "));
        }
        if (s.three == "partial") {
            need << "3-horn parts (folder exists but blank/score-only)";
        } else if (s.three == "none") {
            need << "3-horn arrangement";
        }
        b += QString("<tr><td class=\"mono\" style=\"font-weight:700;color:#16213e;\">%1</td><td>%2</td><td class=\"c\">%3</td>"
                     "<td class=\"c\">%4</td><td class=\"c\">%5</td><td class=\"dim\">%6</td></tr>")
             .arg(d->code, esc(d->name), mark(s.lead), mark(s.rhythm), mark(s.three), need.join("; "));
    }
    b += "</table><div class=\"legend\"><span><span class=\"sq s-done\"></span> done</span><span><span class=\"sq s-part\"></span> started</span>"
         "<span><span class=\"sq s-none\"></span> not started</span></div>"
         "<div class=\"foot\"><span>Starsign &middot; Progress tracker</span><span>Phase 1 &middot; the urgent one</span></div>"
         "<div class=\"pagebreak\"></div>";

    // phase 2
    int counts[8] = { 0 };
    for (const SongInfo* d : songs) {
        for (int n = 1; n <= 7; ++n) {
            counts[n] += d->status.horns.at(n) == "done";
        }
    }
    b += "<h1>Phase 2 &mdash; 1 to 7 horn coverage</h1><div class=\"sub\">The long quest. Start it once Phase 1 is clear.</div><div class=\"tiles\">";
    int doneCells = 0;
    for (int n = 1; n <= 7; ++n) {
        doneCells += counts[n];
        b += QString("<div class=\"tile\"><div class=\"big\" style=\"font-size:21pt;\">%1<span style=\"font-size:12pt;color:#8b90a0;\">/%2</span>"
                     "</div><div class=\"lab\">%3 horn</div></div>").arg(counts[n]).arg(tot).arg(n);
    }
    const int totalCells = tot * 7;
    b += QString("</div><div class=\"bar\"><i class=\"s-done\" style=\"width:%1%\"></i></div><div class=\"legend\"><b style=\"color:#16213e;\">"
                 "%2 of %3 arrangements written (%4%).</b></div>")
         .arg(100.0 * doneCells / totalCells, 0, 'f', 1).arg(doneCells).arg(totalCells).arg(std::lround(100.0 * doneCells / totalCells));
    b += "<table class=\"mtx\" style=\"margin-top:14px;\"><tr><th style=\"width:34px\">Code</th><th>Song</th>";
    for (int n = 1; n <= 7; ++n) {
        b += QString("<th class=\"c\" style=\"width:30px\">%1H</th>").arg(n);
    }
    b += "<th class=\"c\" style=\"width:44px\">Done</th></tr>";
    QString lastCat;
    for (const SongInfo* d : songs) {
        if (d->cat != lastCat) {
            lastCat = d->cat;
            b += QString("<tr><td class=\"grp\" colspan=\"10\">%1</td></tr>").arg(CAT_LABEL.value(lastCat));
        }
        QString row;
        int got = 0;
        for (int n = 1; n <= 7; ++n) {
            row += "<td class=\"c\">" + mark(d->status.horns.at(n)) + "</td>";
            got += d->status.horns.at(n) == "done";
        }
        b += QString("<tr><td class=\"mono\" style=\"font-weight:700;color:#16213e;\">%1</td><td>%2</td>%3<td class=\"c dim\">%4/7</td></tr>")
             .arg(d->code, esc(d->name), row).arg(got);
    }
    b += "</table><div class=\"legend\"><span><span class=\"sq s-done\"></span> parts written</span><span><span class=\"sq s-part\"></span> "
         "folder exists but parts are blank or score-only</span><span><span class=\"sq s-none\"></span> nothing yet</span></div>"
         "<div class=\"foot\"><span>Starsign &middot; Progress tracker</span><span>Phase 2 &middot; horn coverage</span></div>"
         "<div class=\"pagebreak\"></div>";

    // page 3: blank and short sheets
    int blanks = 0, shorts = 0, affected = 0;
    for (const SongInfo* d : songs) {
        for (const SheetIssue& i : d->issues) {
            (i.quality == Quality::Empty ? blanks : shorts)++;
        }
        affected += !d->issues.empty();
    }
    b += QString("<h1>Sheets that exist but are not written</h1><div class=\"sub\">Detected by measuring how much music is actually on each "
                 "page. A &ldquo;blank&rdquo; sheet has the form and rehearsal marks laid out but no notes.</div><div class=\"tiles\">"
                 "<div class=\"tile\"><div class=\"big\">%1</div><div class=\"lab\">Blank sheets</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%2</div><div class=\"lab\">Suspiciously short</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%3</div><div class=\"lab\">Tunes affected</div></div></div>")
         .arg(blanks).arg(shorts).arg(affected);
    b += "<table class=\"mtx\" style=\"margin-top:12px;\"><tr><th style=\"width:34px\">Code</th><th style=\"width:26%\">Song</th>"
         "<th style=\"width:32%\">Blank</th><th>Short</th></tr>";
    for (const SongInfo* d : songs) {
        if (d->issues.empty()) {
            continue;
        }
        auto fmt = [&](bool blank) {
            QStringList out;
            for (const SheetIssue& i : d->issues) {
                if ((i.quality == Quality::Empty) != blank) {
                    continue;
                }
                QString nm = i.file;
                if (nm.startsWith(d->code + " - ")) {
                    nm = nm.mid(d->code.size() + 3);
                }
                if (nm.endsWith(".pdf")) {
                    nm.chop(4);
                }
                out << QString("%1 <span class=\"dim\">(%2)</span>").arg(esc(nm), esc(i.folder));
            }
            return out.isEmpty() ? QString("<span class=\"dim\">&mdash;</span>") : out.join(", ");
        };
        b += QString("<tr><td class=\"mono\" style=\"font-weight:700;color:#16213e;\">%1</td><td>%2</td>"
                     "<td style=\"color:#a8391f;font-size:8.6pt;\">%3</td><td style=\"color:#8a6d1f;font-size:8.6pt;\">%4</td></tr>")
             .arg(d->code, esc(d->name), fmt(true), fmt(false));
    }
    b += "</table><div class=\"foot\"><span>Starsign &middot; Progress tracker</span><span>Unfinished sheets</span></div>"
         "<div class=\"pagebreak\"></div>";

    // the checklist
    b += QString("<h1>The checklist</h1><div class=\"sub\">Every outstanding task, highest priority first. Within each band, tunes are "
                 "ordered by how often Starsign plays them &mdash; plays in %1 plus %2, from setlist.fm. The figures on the right are "
                 "<b>%1 / %2</b>.</div>").arg(y - 1).arg(y);
    b += "<div class=\"note\">Tunes with a <b>3H Flexible</b> chart rather than a dedicated Tpt/Alt/Ten one count as done here &mdash; "
         "those cover the same three chairs, so rewriting them for named horns is optional polish, not a gap.</div>";
    for (const auto& tier : TIERS) {
        std::vector<const Task*> items;
        for (const Task& t : tasks) {
            if (t.tier == tier[0]) {
                items.push_back(&t);
            }
        }
        if (items.empty()) {
            continue;
        }
        b += QString("<div class=\"tierhd\"><span class=\"t\">%1</span><span class=\"c\">%2</span></div>").arg(tier[1], plural(int(items.size()), "task"));
        if (!tier[2].isEmpty()) {
            b += "<div class=\"tierbl\">" + tier[2] + "</div>";
        }
        b += "<div class=\"cols\">";
        for (const Task* t : items) {
            const QString pc = t->lastYear || t->thisYear ? QString("%1/%2").arg(t->lastYear).arg(t->thisYear) : QString("&mdash;");
            b += QString("<div class=\"tk%1\"><span class=\"cb\"></span><span class=\"no\">%2</span><span class=\"nm\">%3%4</span>"
                         "<span class=\"ac\">%5</span><span class=\"pc\">%6</span></div>")
                 .arg(t->sc >= 8 ? " hot" : "").arg(t->n).arg(esc(t->song), t->wip ? " <span style=\"color:#a2a8b8;\">(WIP)</span>" : "",
                                                              t->shortText, pc);
        }
        b += "</div>";
    }
    b += "<div class=\"tierhd\"><span class=\"t\">Someday, when the list above is done</span><span class=\"c\">not counted</span></div>"
         "<div class=\"tierbl\">Not on the checklist, but these are all live options whenever you want a change of pace:</div>"
         "<ul style=\"font-size:9.3pt;margin-top:2px;\"><li><b>Big band charts</b> &mdash; 5 sax / 4 trumpet / 4 trombone / rhythm. "
         "Folder name: <span class=\"mono\">Big Band</span>.</li><li><b>Orchestral arrangements</b> &mdash; folder name: "
         "<span class=\"mono\">Full Orchestra</span>.</li><li><b>String quartet and quintet</b> &mdash; folder names like "
         "<span class=\"mono\">4S 2Vln Vla Vc</span>.</li><li><b>Marching band arrangements</b> &mdash; folder name: "
         "<span class=\"mono\">Marching Band</span>.</li><li><b>Sellable versions of the sheets</b> &mdash; a project in its own right. "
         "Every chart would need a fine-toothed pass for engraving, consistency and correctness before it could go out under your name.</li></ul>";
    if (!plays.notInLibrary.isEmpty()) {
        QStringList names;
        for (const QString& n : plays.notInLibrary) {
            names << "<b>" + esc(n) + "</b>";
        }
        b += "<div class=\"note\">setlist.fm also lists " + names.join(", ") + " as played, and there is no folder for any of them.</div>";
    }
    b += QString("<div class=\"foot\"><span>Starsign &middot; Progress tracker</span><span>The checklist &middot; made %1 &middot; this PDF "
                 "is auto-generated, do not edit it by hand</span></div>").arg(prettyDate(today.toString(Qt::ISODate)));
    return htmlPage("Starsign Progress Tracker", b, HOME_CSS);
}
}

namespace mu::project::starscore::org {
// ------------------------------------------------------------------ maintenance report (gen_maintlog.py)
static const char* LOG_CSS = R"CSS(
.hero{background:#16213e;color:#fff;margin:0 0 14px;padding:20px 24px 18px;border-radius:8px;}
.hero h1{color:#fff;font-size:25pt;margin:0;}
.hero .sub{color:#a9b4d0;font-size:10.5pt;margin-top:4px;}
.tiles{display:flex;gap:10px;margin:12px 0 4px;}
.tile{background:#f7f8fb;border:1px solid #e3e7f0;border-radius:7px;padding:9px 12px;flex:1;}
.tile .big{font-size:20pt;font-weight:700;color:#16213e;line-height:1;}
.tile .lab{font-size:7.8pt;text-transform:uppercase;letter-spacing:.6px;color:#6d7387;margin-top:4px;}
.status{width:100%;border-collapse:collapse;margin-top:4px;}
.status td{padding:4px 8px 4px 0;font-size:9.5pt;border-bottom:1px solid #f0f2f7;vertical-align:top;}
.status td.kk{color:#6d7387;width:34%;}
.status td.v{color:#1b2030;font-weight:600;}
.entry{margin:15px 0 0;}
.stamp{display:flex;justify-content:space-between;align-items:baseline;border-bottom:1.5px solid #16213e;padding-bottom:3px;margin-bottom:6px;break-after:avoid;}
.stamp .d{font-size:12.5pt;font-weight:700;color:#16213e;font-family:'SF Mono',Menlo,Consolas,monospace;}
.stamp .t{font-size:8.4pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;}
.entry.old .stamp{border-bottom-color:#cfd4e0;} .entry.old .stamp .d{color:#6d7387;font-size:11pt;}
.act{font-size:9.5pt;margin:4px 0;padding-left:15px;position:relative;line-height:1.4;break-inside:avoid;}
.act:before{content:'';position:absolute;left:2px;top:5px;width:5px;height:5px;border-radius:50%;background:#2f7d5c;}
.act.change:before{background:#c8a24a;}
.act.warn:before{background:#a8391f;}
.act .h{font-weight:700;color:#16213e;}
.mono{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:8.8pt;}
.note{background:#f7f8fb;border-left:3px solid #c8a24a;padding:9px 12px;margin:11px 0;font-size:9.3pt;color:#3d4356;border-radius:0 4px 4px 0;}
)CSS";

QString logEntryHtml(const QJsonObject& e, bool newest)
{
    QString b = QString("<div class=\"entry%1\"><div class=\"stamp\"><span class=\"d\">%2</span><span class=\"t\">%3</span></div>")
                .arg(newest ? "" : " old", esc(e.value("date").toString()), esc(e.value("title").toString("maintenance run")));
    for (const QJsonValue& v : e.value("actions").toArray()) {
        const QJsonObject a = v.toObject();
        const QString head = a.value("head").toString();
        b += QString("<div class=\"act %1\">%2%3</div>").arg(a.value("kind").toString(),
                                                              head.isEmpty() ? QString() : "<span class=\"h\">" + head + "</span> &mdash; ",
                                                              a.value("text").toString());
    }
    return b + "</div>";
}

QString maintenanceHtml(const Library& lib, const std::vector<Task>& tasks, const QJsonArray& log, const QDate& today, int hornGuides,
                        int changelogs)
{
    const int tot = int(lib.songs.size());
    int p1 = 0, blanks = 0, shorts = 0, guides = 0;
    for (const SongInfo& d : lib.songs) {
        p1 += d.phase1();
        for (const SheetIssue& i : d.issues) {
            (i.quality == Quality::Empty ? blanks : shorts)++;
        }
    }
    guides = tot;
    // The PDF shows the last twelve months. maintlog.json keeps every entry forever; printing all of them made the
    // report grow by a page or so every month (Oct 2026).
    const QString since = today.addMonths(-12).toString(Qt::ISODate);
    QJsonArray recent;
    int older = 0;
    for (const QJsonValue& v : log) {
        if (v.toObject().value("date").toString() >= since) {
            recent.append(v);
        } else {
            ++older;
        }
    }
    QString b = "<div class=\"hero\"><h1>Maintenance report</h1><div class=\"sub\">Starsign &middot; Sheets and Demos &middot; every "
                "update to this drive in the last twelve months, newest first</div></div>";
    b += QString("<div class=\"tiles\"><div class=\"tile\"><div class=\"big\">%1</div><div class=\"lab\">Files tracked</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%2</div><div class=\"lab\">Songs</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%3<span style=\"font-size:12pt;color:#8b90a0;\">/%2</span></div><div class=\"lab\">Phase 1 done</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%4</div><div class=\"lab\">Open tasks</div></div>"
                 "<div class=\"tile\"><div class=\"big\" style=\"color:#a8391f;\">%5</div><div class=\"lab\">Blank sheets</div></div></div>")
         .arg(thousands(lib.totalFiles)).arg(tot).arg(p1).arg(tasks.size()).arg(blanks);
    const QString top = tasks.empty() ? QString("nothing outstanding") : esc(tasks[0].song) + " &mdash; " + tasks[0].text;
    const std::vector<std::pair<QString, QString> > rows {
        { "Top of the checklist", top },
        { "Phase 1 (lead + rhythm + 3-horn)", QString("%1 of %2 songs complete").arg(p1).arg(tot) },
        { "Sheets that exist but are blank", QString("%1 across the library").arg(blanks) },
        { "Sheets that look unfinished", QString("%1 flagged as running short").arg(shorts) },
        { "Generated PDFs", QString("%1 song guides &middot; %2 recordings pages &middot; %3 horn part guides &middot; %4 changelogs "
                                    "&middot; 3 top-level").arg(guides).arg(guides).arg(hornGuides).arg(changelogs) },
        { "Archive", QString("%1 files preserved, nothing deleted").arg(thousands(lib.archivedFiles)) },
        { "Inbox", QString("%1 waiting to be filed").arg(plural(lib.inboxFiles, "file")) },
        { "When it runs", "every time Joel exports from StarScore Studio, or presses Run Folder Organization" },
    };
    b += "<h2>Where things stand</h2><table class=\"status\">";
    for (const auto& [k, v] : rows) {
        b += QString("<tr><td class=\"kk\">%1</td><td class=\"v\">%2</td></tr>").arg(k, v);
    }
    b += "</table><div class=\"note\">This log is added to, never overwritten. If something looks wrong after an update, the entry that "
         "caused it is right here.</div><h2>Session log</h2>"
         "<p class=\"dim\" style=\"font-size:8.8pt;margin:0 0 4px;\">The last twelve months (since " + prettyDate(since) + "). "
         + (older ? QString("%1 older %2 in <span class=\"mono\">6 Inbox/.organizer/maintlog.json</span>, which keeps every entry.")
            .arg(older).arg(older == 1 ? "entry is" : "entries are")
            : QString("Every entry so far is shown; <span class=\"mono\">6 Inbox/.organizer/maintlog.json</span> keeps them all."))
         + "</p>";
    bool newest = true;
    for (const QJsonValue& v : recent) {
        b += logEntryHtml(v.toObject(), newest);
        newest = false;
    }
    b += QString("<div class=\"foot\"><span>Starsign &middot; Maintenance report</span><span>made %1 &middot; this PDF is auto-generated, "
                 "do not edit it by hand</span></div>").arg(prettyDate(today.toString(Qt::ISODate)));
    return htmlPage("Starsign Maintenance Report", b, LOG_CSS);
}

// ------------------------------------------------------------------ Projects Maintenance Report (gen_projlog.py)
QString projectsReportHtml(const QJsonObject& state, const QDate& today)
{
    QString css = LOG_CSS;
    css += R"CSS(
.hero{background:#2c3a2e;} .hero .sub{color:#adc0b0;}
h2{color:#2c3a2e;border-bottom-color:#2c3a2e;}
.tile{background:#f6f8f6;border-color:#e0e7e1;} .tile .big{color:#2c3a2e;}
.stamp{border-bottom-color:#2c3a2e;} .stamp .d{color:#2c3a2e;} .act .h{color:#2c3a2e;}
.src{width:100%;border-collapse:collapse;margin-top:4px;}
.src th{font-size:8.2pt;text-transform:uppercase;letter-spacing:.6px;color:#6d7387;padding:0 6px 4px 0;border-bottom:1.5px solid #2c3a2e;text-align:left;}
.src td{padding:3px 6px 3px 0;font-size:9.1pt;border-bottom:1px solid #f2f4f8;vertical-align:top;}
.src td.n{font-weight:600;color:#1b2030;}
.ok{color:#2f7d5c;font-weight:700;} .no{color:#a8391f;font-weight:700;} .dimx{color:#8b90a0;}
.lede{font-size:9.2pt;color:#6d7387;margin:2px 0 8px;line-height:1.45;}
.note{background:#f6f8f6;}
)CSS";
    const QJsonArray tunes = state.value("tunes").toArray();
    int withStarScore = 0;
    for (const QJsonValue& v : tunes) {
        withStarScore += v.toObject().value("starscore").toBool();
    }
    QString b = "<div class=\"hero\"><h1>Maintenance report</h1><div class=\"sub\">Projects and Sheets &middot; Joel&rsquo;s working "
                "files &middot; every update, newest first</div></div>";
    b += QString("<div class=\"tiles\"><div class=\"tile\"><div class=\"big\">%1</div><div class=\"lab\">Files</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%2</div><div class=\"lab\">StarScore files</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%3</div><div class=\"lab\">Older MuseScore files</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%4</div><div class=\"lab\">Tune folders</div></div>"
                 "<div class=\"tile\"><div class=\"big\" style=\"color:#a8391f;\">%5</div><div class=\"lab\">Quarantined</div></div></div>")
         .arg(thousands(state.value("files").toInt())).arg(state.value("starscore").toInt()).arg(thousands(state.value("mscz").toInt()))
         .arg(tunes.size()).arg(state.value("quarantined").toInt());
    const std::vector<std::pair<QString, QString> > rows {
        { "Structure", "grouped by purpose, then tune &mdash; Starsign Originals, Starsign Covers, Starsign WIP, Other Projects" },
        { "The live file", "each tune&rsquo;s <span class=\"mono\">CODE - Title.starscore</span>, at the top of its folder. Older "
                           "MuseScore files live in <span class=\"mono\">MuseScore Files/</span>." },
        { "Drop zone", "<span class=\"mono\">9 Inbox</span> &mdash; filed whenever StarScore runs the folder organization" },
        { "Templates", QString("%1 real templates kept in <span class=\"mono\">6 Templates</span>").arg(state.value("templates").toInt()) },
        { "Quarantine", QString("%1 byte-identical copies of an empty MuseScore template").arg(state.value("quarantined").toInt()) },
        { "Deletions", "none, ever &mdash; superseded files go to <span class=\"mono\">Version History/</span> or "
                       "<span class=\"mono\">Deprecated/</span>" },
    };
    b += "<h2>Where things stand</h2><table class=\"status\">";
    for (const auto& [k, v] : rows) {
        b += QString("<tr><td class=\"kk\">%1</td><td class=\"v\">%2</td></tr>").arg(k, v);
    }
    b += "</table>";
    QStringList missing;
    for (const QJsonValue& v : tunes) {
        const QJsonObject t = v.toObject();
        if (!t.value("starscore").toBool() && t.value("group").toString().contains("Starsign")) {
            missing << "<b>" + esc(t.value("name").toString()) + "</b>";
        }
    }
    if (!missing.isEmpty()) {
        b += "<div class=\"note\"><b>No StarScore file yet for " + missing.join(", ") + ".</b></div>";
    }
    b += QString("<h2>What is in each tune folder</h2><p class=\"lede\">%1 of %2 tune folders have their StarScore file.</p>"
                 "<table class=\"src\"><tr><th style=\"width:17%\">Group</th><th style=\"width:27%\">Tune</th><th style=\"width:8%\">Code</th>"
                 "<th style=\"width:11%\">StarScore</th><th>MuseScore Files</th><th>Version History</th><th>Deprecated</th></tr>")
         .arg(withStarScore).arg(tunes.size());
    auto num = [](int n) { return n ? QString::number(n) : QString("<span class=\"dimx\">&mdash;</span>"); };
    for (const QJsonValue& v : tunes) {
        const QJsonObject t = v.toObject();
        b += QString("<tr><td class=\"dimx\" style=\"font-size:8.4pt;\">%1</td><td class=\"n\">%2</td><td class=\"mono\">%3</td><td>%4</td>"
                     "<td>%5</td><td>%6</td><td>%7</td></tr>")
             .arg(esc(t.value("group").toString().mid(2)), esc(t.value("name").toString()), esc(t.value("code").toString()),
                  t.value("starscore").toBool() ? QString("<span class=\"ok\">&#10003;</span>") : QString("<span class=\"no\">none</span>"),
                  num(t.value("museScoreFiles").toInt()), num(t.value("versionHistory").toInt()), num(t.value("deprecated").toInt()));
    }
    b += "</table><h2>Session log</h2>";
    bool newest = true;
    for (const QJsonValue& v : state.value("log").toArray()) {
        b += logEntryHtml(v.toObject(), newest);
        newest = false;
    }
    b += QString("<div class=\"foot\"><span>Projects and Sheets &middot; maintenance report</span><span>made %1 &middot; this PDF is "
                 "auto-generated, do not edit it by hand</span></div>").arg(prettyDate(today.toString(Qt::ISODate)));
    return htmlPage("Projects Maintenance Report", b, css);
}
}
