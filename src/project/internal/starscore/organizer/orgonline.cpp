/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: reading setlist.fm and YouTube pages
 */
#include "orgonline.h"

#include <QJsonDocument>
#include <QSet>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace mu::project::starscore::org {
static const QRegularExpression::PatternOption DOTALL = QRegularExpression::DotMatchesEverythingOption;

QString htmlUnescape(const QString& in)
{
    static const QRegularExpression ent("&(#x[0-9a-fA-F]+|#[0-9]+|[a-zA-Z]+);");
    static const QMap<QString, QString> named {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "nbsp", " " },
        { "ndash", QString(QChar(0x2013)) }, { "mdash", QString(QChar(0x2014)) }, { "hellip", QString(QChar(0x2026)) },
        { "rsquo", QString(QChar(0x2019)) }, { "lsquo", QString(QChar(0x2018)) }, { "ldquo", QString(QChar(0x201C)) },
        { "rdquo", QString(QChar(0x201D)) },
    };
    // Latin-1 letters by name (&auml; etc.)
    static const QMap<QString, QString> latin = []() {
        QMap<QString, QString> m;
        const char* names[] = { "Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc",
                                "Euml", "Igrave", "Iacute", "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml",
                                "times", "Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute",
                                "acirc", "atilde", "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave",
                                "iacute", "icirc", "iuml", "eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide",
                                "oslash", "ugrave", "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml" };
        for (int i = 0; i < 64; ++i) {
            m[names[i]] = QString(QChar(0xC0 + i));
        }
        return m;
    }();
    QString out;
    int last = 0;
    auto it = ent.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += in.mid(last, m.capturedStart() - last);
        const QString e = m.captured(1);
        if (e.startsWith('#')) {
            const char32_t c = e.startsWith("#x") ? e.mid(2).toUInt(nullptr, 16) : e.mid(1).toUInt();
            out += QString::fromUcs4(&c, 1);
        } else {
            out += named.value(e, latin.value(e, m.captured(0)));
        }
        last = m.capturedEnd();
    }
    out += in.mid(last);
    return out;
}

static QString stripTags(const QString& s)
{
    static const QRegularExpression tag("<[^>]*>");
    QString t = s;
    t.replace(tag, " ");
    return htmlUnescape(t).simplified();
}

static int monthNumber(const QString& name)
{
    static const QStringList months { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    return months.indexOf(name.left(3).toLower()) + 1;
}

static QString attr(const QString& tag, const QString& name)
{
    // called for every <td> of a stats page: the few attribute names it is asked for keep their pattern
    // (the pages are parsed on the main thread only, so a plain static map is fine)
    static QMap<QString, QRegularExpression> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        it = cache.insert(name, QRegularExpression("\\b" + QRegularExpression::escape(name) + "\\s*=\\s*\"([^\"]*)\""));
    }
    const QRegularExpressionMatch m = it->match(tag);
    return m.hasMatch() ? htmlUnescape(m.captured(1)) : QString();
}

static void splitVenue(const QString& atText, QString& venue, QString& city)
{
    const int comma = atText.indexOf(", ");
    venue = comma < 0 ? atText.trimmed() : atText.left(comma).trimmed();
    city = comma < 0 ? QString() : atText.mid(comma + 2).trimmed();
}

static QString absoluteSetlistUrl(const QString& href)
{
    QString h = href;
    while (h.startsWith("../")) {
        h = h.mid(3);
    }
    if (h.startsWith("http")) {
        return h;
    }
    if (h.startsWith('/')) {
        return "https://www.setlist.fm" + h;
    }
    return "https://www.setlist.fm/" + h;
}

std::vector<SetlistShow> parseSetlistList(const QString& html)
{
    std::vector<SetlistShow> shows;
    static const QRegularExpression start("<div[^>]*class=\"[^\"]*\\bsetlistPreview\\b[^\"]*\"");
    QList<qsizetype> starts;
    auto it = start.globalMatch(html);
    while (it.hasNext()) {
        starts << it.next().capturedStart();
    }
    for (int i = 0; i < starts.size(); ++i) {
        const QString block = html.mid(starts[i], (i + 1 < starts.size() ? starts[i + 1] : html.size()) - starts[i]);
        static const QRegularExpression month("class=\"month\"[^>]*>\\s*([A-Za-z]+)");
        static const QRegularExpression day("class=\"day\"[^>]*>\\s*(\\d+)");
        static const QRegularExpression year("class=\"year\"[^>]*>\\s*(\\d{4})");
        static const QRegularExpression link("<h2[^>]*>\\s*<a([^>]*)>(.*?)</a>", DOTALL);
        const auto mm = month.match(block), dm = day.match(block), ym = year.match(block), lm = link.match(block);
        if (!mm.hasMatch() || !dm.hasMatch() || !ym.hasMatch() || !lm.hasMatch()) {
            continue;
        }
        SetlistShow s;
        s.date = QDate(ym.captured(1).toInt(), monthNumber(mm.captured(1)), dm.captured(1).toInt());
        s.url = absoluteSetlistUrl(attr(lm.captured(1), "href"));
        s.title = stripTags(lm.captured(2));
        const int at = s.title.indexOf(" at ");
        splitVenue(at < 0 ? s.title : s.title.mid(at + 4), s.venue, s.city);

        static const QRegularExpression summary("class=\"[^\"]*\\bsetSummary\\b[^\"]*\"(.*?)</ol>", DOTALL);
        const auto sm = summary.match(block);
        if (sm.hasMatch()) {
            static const QRegularExpression li("<li[^>]*>(.*?)</li>", DOTALL);
            auto lit = li.globalMatch(sm.captured(1));
            while (lit.hasNext()) {
                const QString song = stripTags(lit.next().captured(1));
                if (!song.isEmpty()) {
                    s.songs << song;
                }
            }
        }
        if (s.date.isValid() && !s.url.isEmpty()) {
            shows.push_back(s);
        }
    }
    return shows;
}

int parseSetlistPageCount(const QString& html)
{
    static const QRegularExpression page("[?&]page=(\\d+)");
    int n = 1;
    auto it = page.globalMatch(html);
    while (it.hasNext()) {
        n = std::max(n, it.next().captured(1).toInt());
    }
    return n;
}

SetlistDetail parseSetlistPage(const QString& html)
{
    SetlistDetail d;
    static const QRegularExpression title("<title>(.*?)</title>", DOTALL);
    const auto tm = title.match(html);
    if (tm.hasMatch()) {
        const QString t = htmlUnescape(tm.captured(1)).simplified();
        static const QRegularExpression parts("Setlist at (.*) on ([A-Za-z]+) (\\d{1,2}), (\\d{4})");
        const auto pm = parts.match(t);
        if (pm.hasMatch()) {
            splitVenue(pm.captured(1), d.venue, d.city);
            d.date = QDate(pm.captured(4).toInt(), monthNumber(pm.captured(2)), pm.captured(3).toInt());
        }
    }
    static const QRegularExpression song("<a[^>]*class=\"[^\"]*\\bsongLabel\\b[^\"]*\"[^>]*>(.*?)</a>", DOTALL);
    auto it = song.globalMatch(html);
    while (it.hasNext()) {
        const QString s = stripTags(it.next().captured(1));
        if (!s.isEmpty()) {
            d.songs << s;
        }
    }
    return d;
}

QMap<QString, int> parseSetlistStats(const QString& html)
{
    QMap<QString, int> counts;
    static const QRegularExpression td("<td\\b[^>]*>");
    QString pendingName;
    bool haveName = false;
    auto it = td.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QString cls = attr(tag, "class");
        if (cls.contains("songName")) {
            pendingName = attr(tag, "data-stats-sort").simplified();
            haveName = !pendingName.isEmpty();
        } else if (cls.contains("songCount") && haveName) {
            counts[pendingName] += attr(tag, "data-stats-sort").toInt();
            haveName = false;
        }
    }
    return counts;
}

QString youTubeId(const QString& link)
{
    const QString l = link.trimmed();
    static const QRegularExpression bare("^[A-Za-z0-9_-]{11}$");
    if (bare.match(l).hasMatch()) {
        return l;
    }
    const QUrl url = QUrl::fromUserInput(l);
    const QString host = url.host().toLower();
    if (host.endsWith("youtu.be")) {
        return url.path().mid(1).left(11);
    }
    if (host.contains("youtube.com")) {
        const QString v = QUrlQuery(url).queryItemValue("v");
        if (!v.isEmpty()) {
            return v.left(11);
        }
        static const QRegularExpression path("^/(?:live|shorts|embed|v)/([A-Za-z0-9_-]{11})");
        const auto m = path.match(url.path());
        if (m.hasMatch()) {
            return m.captured(1);
        }
    }
    return QString();
}

QJsonObject extractJsonAssignment(const QString& html, const QString& name)
{
    const QRegularExpression re(QRegularExpression::escape(name) + "\\s*=\\s*\\{");
    const auto m = re.match(html);
    if (!m.hasMatch()) {
        return QJsonObject();
    }
    const qsizetype begin = m.capturedEnd() - 1;
    int depth = 0;
    bool inString = false;
    QChar quote;
    for (qsizetype i = begin; i < html.size(); ++i) {
        const QChar c = html.at(i);
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == quote) {
                inString = false;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            inString = true;
            quote = c;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) {
                return QJsonDocument::fromJson(html.mid(begin, i - begin + 1).toUtf8()).object();
            }
        }
    }
    return QJsonObject();
}

bool parseYouTubeWatchPage(const QString& html, YouTubeVideo& out)
{
    const QJsonObject player = extractJsonAssignment(html, "ytInitialPlayerResponse");
    const QJsonObject details = player.value("videoDetails").toObject();
    if (details.isEmpty()) {
        return false;
    }
    out.id = details.value("videoId").toString();
    out.title = details.value("title").toString();
    out.description = details.value("shortDescription").toString();
    out.lengthSeconds = details.value("lengthSeconds").toString().toInt();
    const QJsonObject micro = player.value("microformat").toObject().value("playerMicroformatRenderer").toObject();
    QString date = micro.value("publishDate").toString();
    if (date.isEmpty()) {
        date = micro.value("uploadDate").toString();
    }
    out.published = QDate::fromString(date.left(10), Qt::ISODate);
    return !out.id.isEmpty();
}

std::vector<Timestamp> parseTimestamps(const QString& description)
{
    std::vector<Timestamp> out;
    static const QRegularExpression ts("(?<![\\d:])(\\d{1,2}:)?(\\d{1,2}):(\\d{2})(?![\\d:])");
    for (const QString& rawLine : description.split('\n')) {
        const QString line = rawLine.trimmed();
        const auto m = ts.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        const int h = m.captured(1).isEmpty() ? 0 : m.captured(1).chopped(1).toInt();
        const int seconds = h * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toInt();
        // the name is whatever is left on the line once the time and the separators around it are removed
        QString name = (line.left(m.capturedStart()) + " " + line.mid(m.capturedEnd())).trimmed();
        static const QRegularExpression edge("^[\\s\\-–—:|•·.)\\]]+|[\\s\\-–—:|•·.(\\[]+$");
        name.replace(edge, "");
        static const QRegularExpression number("^\\d{1,2}[.)]\\s+");   // "3. Amplitudes"
        name.replace(number, "");
        name = name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        out.push_back({ seconds, name });
    }
    return out;
}

QString normalizeName(const QString& s)
{
    QString t = s.normalized(QString::NormalizationForm_D);
    QString out;
    for (const QChar c : t) {
        if (c.category() == QChar::Mark_NonSpacing) {
            continue;
        }
        out += c;
    }
    out = out.toLower();
    out.replace('&', " and ");
    out.replace(QChar(0x2019), "'");
    static const QRegularExpression punct("[^a-z0-9]+");
    out.replace(punct, " ");
    return out.simplified();
}

NameMatcher::NameMatcher(const QMap<QString, QString>& aliases, const QStringList& skipNames)
{
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        byNorm[normalizeName(it.key())] = it.value();
    }
    for (const QString& s : skipNames) {
        skip.insert(normalizeName(s));
    }
}

MatchedTimestamp matchTimestamp(const Timestamp& t, const QMap<QString, QString>& aliases, const QStringList& skipNames)
{
    return NameMatcher(aliases, skipNames).match(t);
}

MatchedTimestamp NameMatcher::match(const Timestamp& t) const
{
    MatchedTimestamp r;
    r.seconds = t.seconds;
    r.name = t.name;

    // the name split from its brackets: "Branston Pickle (Piano Intro)" -> "Branston Pickle" + "Piano Intro"
    static const QRegularExpression bracket("\\(([^)]*)\\)");
    QString base = t.name;
    QStringList notes;
    auto it = bracket.globalMatch(t.name);
    while (it.hasNext()) {
        notes << it.next().captured(1).trimmed();
    }
    base.replace(bracket, " ");
    base = base.simplified();

    QStringList variants;
    for (const QString& n : notes) {
        const QString low = n.toLower();
        if (low.contains("cover") || low.startsWith("by ")) {
            continue;     // "(Snarky Puppy cover)", "(by Snarky Puppy)" say whose song it is
        }
        if (low == "intro") {
            variants << "intro only";
        } else {
            variants << ((low.startsWith("ft") || low.startsWith("feat")) ? n : low);
        }
    }
    r.variant = variants.join(", ");

    const QString full = normalizeName(t.name);
    if (skip.contains(full) || skip.contains(normalizeName(base))) {
        r.code = "-";
        return r;
    }
    if (byNorm.contains(full)) {
        r.code = byNorm.value(full);
        return r;
    }
    r.code = byNorm.value(normalizeName(base));
    return r;
}
}
