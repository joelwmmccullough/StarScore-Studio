/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the chord chart editor (1.30.0). See chordcharteditormodel.h.
 */
#include "chordcharteditormodel.h"

#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <set>

using namespace mu::project;

namespace mu::project::ireal {
namespace {
const QRegularExpression& chordRe()
{
    // a root (W: no root, a bass only), its quality (iReal Pro's: "-7", "^7#11", "7b9sus", "h7", "o7", "69"…), a bass
    static const QRegularExpression re("^[A-GW][b#]?(?:sus|alt|add|[-^+oh0-9b#])*(?:/[A-G][b#]?)?");
    return re;
}

const std::set<QString>& qualities()
{
    // iReal Pro's chord qualities (its file format page)
    static const std::set<QString> q {
        "", "5", "2", "add9", "+", "o", "h", "sus", "^", "-", "^7", "-7", "7", "7sus", "h7", "o7", "^9", "^13", "6", "69",
        "^7#11", "^9#11", "^7#5", "-6", "-69", "-^7", "-^9", "-9", "-11", "-7b5", "h9", "-b6", "-#5", "9", "7b9", "7#9",
        "7#11", "7b5", "7#5", "9#11", "9b5", "9#5", "7b13", "7#9#5", "7#9b5", "7#9#11", "7b9#11", "7b9b5", "7b9#5",
        "7b9#9", "7b9b13", "7alt", "13", "13#11", "13#9", "13b9", "11", "7b9sus", "7susadd3", "9sus", "13sus", "7b13sus",
        "-^7#11", "^#11", "-13", "6add9", "^7b5", "+7", "7+", "o^7"
    };
    return q;
}

QString accidentals(QString s)
{
    s.replace('b', QChar(0x266D));
    s.replace('#', QChar(0x266F));
    return s;
}

//! a root as typed ("eb", "E♭", "F#") -> iReal Pro's ("Eb", "F#"); the length taken in `used`
QString rootFrom(const QString& t, int& used)
{
    used = 0;
    if (t.isEmpty() || !QString("ABCDEFGabcdefg").contains(t[0])) {
        return QString();
    }
    QString r = t[0].toUpper();
    used = 1;
    if (t.size() > 1) {
        const QChar c = t[1];
        if (c == 'b' || c == QChar(0x266D)) {
            // ("Bbm7": b is the flat; "Cb" too; but "B" then "b9"? iReal Pro has no root followed by b9 directly)
            r += 'b';
            used = 2;
        } else if (c == '#' || c == QChar(0x266F)) {
            r += '#';
            used = 2;
        }
    }
    return r;
}
}

std::vector<IRealCell> parse(const QString& music)
{
    std::vector<IRealCell> cells;
    IRealCell pending;
    bool small = false;
    const QString s = music;
    int i = 0;
    const int n = int(s.size());
    auto push = [&](const QString& content) {
        pending.content = content;
        pending.small = !content.isEmpty() && !QString("xrnp").contains(content) ? small : false;
        cells.push_back(pending);
        pending = IRealCell();
    };
    while (i < n) {
        const QChar c = s[i];
        if (c == '{' || c == '[' || c == '|') {
            if (pending.left.isEmpty()) {
                pending.left = c;
            }
            ++i;
        } else if (c == '}' || c == ']' || c == 'Z') {
            if (!cells.empty()) {
                cells.back().right = c;
            }
            ++i;
        } else if (c == 'T' && i + 2 < n && s[i + 1].isDigit() && s[i + 2].isDigit()) {
            pending.ts = s.mid(i, 3);
            i += 3;
        } else if (c == '*' && i + 1 < n && QString("ABCDVi").contains(s[i + 1])) {
            pending.mark = s[i + 1];
            i += 2;
        } else if (c == 'N' && i + 1 < n && s[i + 1].isDigit()) {
            pending.ending = s[i + 1].digitValue();
            i += 2;
        } else if (c == '<') {
            const int j = s.indexOf('>', i);
            pending.texts << s.mid(i + 1, (j < 0 ? n : j) - i - 1);
            i = j < 0 ? n : j + 1;
        } else if (QString("SQfYU").contains(c)) {
            pending.symbols += c;
            ++i;
        } else if (c == 's') {
            small = true;
            ++i;
        } else if (c == 'l') {
            small = false;
            ++i;
        } else if (c == ',') {
            ++i;
        } else if (c == ' ') {
            push(QString());
            ++i;
        } else if (QString("xrnp").contains(c)) {
            push(QString(c));
            ++i;
        } else if (c == '(') {
            const int j = s.indexOf(')', i);
            if (!cells.empty()) {
                cells.back().alt = s.mid(i + 1, (j < 0 ? n : j) - i - 1);
            }
            i = j < 0 ? n : j + 1;
        } else {
            const QRegularExpressionMatch m = chordRe().match(s.mid(i));
            if (m.hasMatch() && m.capturedLength() > 0) {
                push(m.captured(0));
                i += int(m.capturedLength());
            } else {
                ++i;   // (something iReal Pro has that the editor doesn't: left out)
            }
        }
    }
    // what was left for a cell that never came (a bar line or text at the very end)
    if (!cells.empty() && !pending.texts.isEmpty()) {
        cells.back().texts << pending.texts;
    }
    return cells;
}

QStringList serialize(const std::vector<IRealCell>& cells)
{
    QStringList lines;
    QString line;
    bool small = false;
    bool prevFilled = false;
    for (size_t i = 0; i < cells.size(); ++i) {
        const IRealCell& c = cells[i];
        if (!c.left.isEmpty()) {
            line += c.left;
            prevFilled = false;
        }
        if (!c.mark.isEmpty()) {
            line += "*" + c.mark;
        }
        line += c.ts;
        if (c.ending > 0) {
            line += QString("N%1").arg(c.ending);
        }
        for (const QString& t : c.texts) {
            line += "<" + t + ">";
        }
        line += c.symbols;
        if (c.content.isEmpty()) {
            line += " ";
            if (!c.alt.isEmpty()) {
                line += "(" + c.alt + ")";   // (an alternate chord over an empty cell)
            }
            prevFilled = false;
        } else {
            const bool chord = !QString("xrnp").contains(c.content);
            if (prevFilled) {
                line += ",";
            }
            if (chord && c.small != small) {
                line += c.small ? "s" : "l";
                small = c.small;
            }
            line += c.content;
            if (!c.alt.isEmpty()) {
                line += "(" + c.alt + ")";
            }
            prevFilled = true;
        }
        if (!c.right.isEmpty()) {
            line += c.right;
            prevFilled = false;
        }
        if ((i + 1) % 16 == 0) {
            lines << line;
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        lines << line;
    }
    return lines;
}

QString chordFromTyped(const QString& typedIn)
{
    QString t = typedIn.trimmed();
    if (t.isEmpty()) {
        return QString();
    }
    const QString low = t.toLower();
    if (low == "n.c." || low == "nc" || low == "n.c" || low == "n") {
        return "n";
    }
    if (t == "%" || low == "x") {
        return "x";
    }
    if (t == "/" || low == "p") {
        return "p";
    }
    // 6/9 is a quality, not a bass
    t.replace("6/9", "69");
    // the bass
    QString bass;
    const int slash = t.indexOf('/');
    if (slash > 0) {
        int used = 0;
        bass = rootFrom(t.mid(slash + 1).trimmed(), used);
        if (bass.isEmpty()) {
            return QString();
        }
        t = t.left(slash);
    }
    int used = 0;
    const QString root = rootFrom(t, used);
    if (root.isEmpty()) {
        return QString();
    }
    QString q = t.mid(used).trimmed();
    q.remove(' ');
    q.replace(QChar(0x266D), 'b');
    q.replace(QChar(0x266F), '#');
    q.replace(QChar(0x0394), "^");   // Δ
    q.replace(QChar(0x2206), "^");   // ∆
    q.replace(QChar(0x00F8), "h");   // ø
    q.replace(QChar(0x00D8), "h");
    q.replace(QChar(0x00B0), "o");   // °
    q.remove('(');
    q.remove(')');
    // the words people type, in iReal Pro's signs (longest first)
    static const std::vector<std::pair<QString, QString> > words {
        { "m7b5", "h7" }, { "min7b5", "h7" }, { "mi7b5", "h7" }, { "-7b5", "h7" },
        { "mmaj7", "-^7" }, { "mMaj7", "-^7" }, { "minmaj7", "-^7" }, { "m^7", "-^7" }, { "mM7", "-^7" },
        { "maj13", "^13" }, { "maj9", "^9" }, { "maj7", "^7" }, { "Maj7", "^7" }, { "ma7", "^7" }, { "M7", "^7" }, { "maj", "^" },
        { "dim7", "o7" }, { "dim", "o" }, { "aug7", "+7" }, { "aug", "+" },
        { "min", "-" }, { "mi", "-" },
        { "sus4", "sus" }, { "6/9", "69" }, { "add2", "add9" },
    };
    for (const auto& [from, to] : words) {
        if (q.startsWith(from)) {
            q = to + q.mid(from.size());
            break;
        }
    }
    if (q.startsWith('m') && !q.startsWith("maj")) {
        q = "-" + q.mid(1);
    }
    q.replace("sus4", "sus");
    if (q == "^") {
        q = "^7";   // (Δ alone is how most write a major seventh)
    }
    if (q == "h") {
        q = "h7";
    }
    const QString chord = root + q + (bass.isEmpty() ? QString() : "/" + bass);
    const QRegularExpressionMatch m = chordRe().match(chord);
    if (!m.hasMatch() || m.capturedLength() != chord.size()) {
        return QString();
    }
    return chord;
}

QStringList chordDisplay(const QString& ireal)
{
    if (ireal == "x") {
        return { "%", QString(), QString() };   // (repeat the bar)
    }
    if (ireal == "r") {
        return { "%%", QString(), QString() };   // (repeat two bars)
    }
    if (ireal == "p") {
        return { "/", QString(), QString() };
    }
    if (ireal == "n") {
        return { "N.C.", QString(), QString() };
    }
    if (ireal.isEmpty()) {
        return { QString(), QString(), QString() };
    }
    QString bass;
    QString c = ireal;
    const int slash = c.indexOf('/');
    if (slash >= 0) {
        bass = accidentals(c.mid(slash + 1));
        c = c.left(slash);
    }
    int used = c.size() > 1 && (c[1] == 'b' || c[1] == '#') ? 2 : 1;
    QString root = c.left(used);
    if (root == "W") {
        root.clear();
    }
    QString q = c.mid(used);
    // (Joel: a half-diminished chord is ø7, never "h")
    q.replace("h", QString(QChar(0x00F8)));
    q.replace("^", QString(QChar(0x0394)));
    q.replace("o", QString(QChar(0x00B0)));
    if (q.startsWith('-')) {
        q = "m" + q.mid(1);
    }
    return { accidentals(root), accidentals(q), bass };
}

bool knownQuality(const QString& ireal)
{
    if (QString("xrnp").contains(ireal) || ireal.isEmpty()) {
        return true;
    }
    QString c = ireal.section('/', 0, 0);
    const int used = c.size() > 1 && (c[1] == 'b' || c[1] == '#') ? 2 : 1;
    return qualities().count(c.mid(used)) > 0;
}

//! iReal Pro's chord as most people type it (what the chord field shows): "Eb-7" -> "Ebm7", "Fh7" -> "Fm7b5"
QString typedFromIReal(const QString& ireal)
{
    if (ireal == "n") {
        return "N.C.";
    }
    if (ireal == "x" || ireal == "r" || ireal == "p") {
        return QString();
    }
    QString c = ireal;
    QString bass;
    const int slash = c.indexOf('/');
    if (slash >= 0) {
        bass = c.mid(slash);
        c = c.left(slash);
    }
    const int used = c.size() > 1 && (c[1] == 'b' || c[1] == '#') ? 2 : 1;
    QString q = c.mid(used);
    if (q == "h7" || q == "h") {
        q = "m7b5";
    } else if (q.startsWith("-^")) {
        q = "mmaj" + q.mid(2);
    } else if (q.startsWith('^')) {
        q = "maj" + q.mid(1);
    } else if (q.startsWith('-')) {
        q = "m" + q.mid(1);
    } else if (q.startsWith('o')) {
        q = "dim" + q.mid(1);
    }
    return c.left(used) + q + bass;
}
}

ChordChartEditorModel::ChordChartEditorModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ChordChartEditorModel::load()
{
    const QVariantMap m = starScore() ? starScore()->irealChart() : QVariantMap();
    m_possible = m.value("possible").toBool();
    m_why = m.value("why").toString();
    m_title = m.value("title").toString();
    m_key = m.value("key").toString();
    m_style = m.value("style").toString();
    m_generated = m.value("generated").toStringList();
    const QStringList edited = m.value("edited").toStringList();
    m_stale = m.value("stale").toBool();
    m_edited = !edited.isEmpty();
    m_cells = ireal::parse((m_edited ? edited : m_generated).join(QString()));
    m_undo.clear();
    m_redo.clear();
    pad();
    m_selected = std::clamp(m_selected, 0, std::max(0, int(m_cells.size()) - 1));
    emit loaded();
    emit cellsChanged();
    emit selectedChanged();
}

void ChordChartEditorModel::pad()
{
    // whole lines; empty lines at the end dropped (one line at least)
    while (m_cells.size() >= 16) {
        bool empty = true;
        for (size_t i = m_cells.size() - 16; i < m_cells.size(); ++i) {
            if (!(m_cells[i] == IRealCell())) {
                empty = false;
                break;
            }
        }
        if (!empty || m_cells.size() == 16) {
            break;
        }
        m_cells.resize(m_cells.size() - 16);
    }
    while (m_cells.empty() || m_cells.size() % 16 != 0) {
        m_cells.push_back(IRealCell());
    }
}

IRealCell* ChordChartEditorModel::cellAt(int index)
{
    return index >= 0 && index < int(m_cells.size()) ? &m_cells[size_t(index)] : nullptr;
}

void ChordChartEditorModel::change(const std::function<void(std::vector<IRealCell>&)>& f)
{
    std::vector<IRealCell> before = m_cells;
    f(m_cells);
    pad();
    if (m_cells == before) {
        return;
    }
    m_undo.push_back(before);
    if (m_undo.size() > 300) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    store();
    emit cellsChanged();
    emit selectedChanged();
}

void ChordChartEditorModel::store()
{
    const QStringList lines = ireal::serialize(m_cells);
    m_edited = lines != m_generated && ireal::serialize(ireal::parse(m_generated.join(QString()))) != lines;
    if (starScore()) {
        starScore()->setIRealEdit(m_edited ? lines : QStringList());
    }
    if (!m_edited) {
        m_stale = false;
    }
}

void ChordChartEditorModel::setMessage(const QString& m)
{
    if (m_message != m) {
        m_message = m;
        emit messageChanged();
    }
}

void ChordChartEditorModel::select(int index)
{
    index = std::clamp(index, 0, std::max(0, int(m_cells.size()) - 1));
    if (m_selected != index) {
        m_selected = index;
        setMessage(QString());
        emit selectedChanged();
    }
}

bool ChordChartEditorModel::setChord(int index, const QString& typed)
{
    if (typed.trimmed().isEmpty()) {
        setContent(index, QString());
        return true;
    }
    const QString chord = ireal::chordFromTyped(typed);
    if (chord.isEmpty()) {
        setMessage(tr("“%1” isn’t a chord StarScore can read (try Ebm7, F7b9, Bbmaj7, Fm7b5, Eb7/Bb or N.C.).").arg(typed));
        return false;
    }
    change([&](std::vector<IRealCell>& cells) {
        if (index >= 0 && index < int(cells.size())) {
            cells[size_t(index)].content = chord;
        }
    });
    setMessage(ireal::knownQuality(chord) ? QString()
               : tr("iReal Pro may not know “%1”: it reads only its own chord qualities.").arg(ireal::chordDisplay(chord).join(QString())));
    return true;
}

void ChordChartEditorModel::setContent(int index, const QString& content)
{
    change([&](std::vector<IRealCell>& cells) {
        if (index >= 0 && index < int(cells.size())) {
            cells[size_t(index)].content = content;
            if (content.isEmpty()) {
                cells[size_t(index)].alt.clear();
                cells[size_t(index)].small = false;
            }
        }
    });
}

#define CHANGE_CELL(body) change([&](std::vector<IRealCell>& cells) { \
        if (index >= 0 && index < int(cells.size())) { IRealCell& c = cells[size_t(index)]; body; } })

void ChordChartEditorModel::setSmall(int index, bool on) { CHANGE_CELL(c.small = on && !c.content.isEmpty()); }
void ChordChartEditorModel::setLeft(int index, const QString& b) { CHANGE_CELL(c.left = b); }
void ChordChartEditorModel::setRight(int index, const QString& b) { CHANGE_CELL(c.right = b); }
void ChordChartEditorModel::setMark(int index, const QString& m) { CHANGE_CELL(c.mark = m); }
void ChordChartEditorModel::setTimeSig(int index, const QString& code) { CHANGE_CELL(c.ts = code); }
void ChordChartEditorModel::setEnding(int index, int e) { CHANGE_CELL(c.ending = std::clamp(e, 0, 3)); }

void ChordChartEditorModel::setRepeatCount(int index, int times)
{
    static const QRegularExpression count("^\\d+x$");
    CHANGE_CELL({
        QStringList keep;
        for (const QString& t : c.texts) {
            if (!count.match(t).hasMatch()) {
                keep << t;
            }
        }
        if (times > 2) {
            keep << QString("%1x").arg(times);
        }
        c.texts = keep;
    });
}

void ChordChartEditorModel::setTextAbove(int index, const QString& text)
{
    CHANGE_CELL({
        QStringList keep;
        for (const QString& t : c.texts) {
            if (!t.startsWith('*')) {
                keep << t;
            }
        }
        QString clean = text;
        clean.remove('<');
        clean.remove('>');
        if (!clean.trimmed().isEmpty()) {
            keep.prepend("*74" + clean.trimmed());   // (as iReal Pro's own charts write "Solos" above a bar)
        }
        c.texts = keep;
    });
}

void ChordChartEditorModel::setTextBelow(int index, const QString& text)
{
    static const QRegularExpression count("^\\d+x$");
    CHANGE_CELL({
        QStringList keep;
        for (const QString& t : c.texts) {
            if (t.startsWith('*') || count.match(t).hasMatch()) {
                keep << t;
            }
        }
        QString clean = text;
        clean.remove('<');
        clean.remove('>');
        if (!clean.trimmed().isEmpty()) {
            keep << clean.trimmed();
        }
        c.texts = keep;
    });
}

void ChordChartEditorModel::setSymbol(int index, const QString& symbol, bool on)
{
    CHANGE_CELL({
        c.symbols.remove(symbol);
        if (on) {
            c.symbols += symbol;
        }
    });
}

void ChordChartEditorModel::setAlt(int index, const QString& typed)
{
    const QString chord = typed.trimmed().isEmpty() ? QString() : ireal::chordFromTyped(typed);
    if (!typed.trimmed().isEmpty() && chord.isEmpty()) {
        setMessage(tr("“%1” isn’t a chord StarScore can read.").arg(typed));
        return;
    }
    CHANGE_CELL(c.alt = chord);
}

void ChordChartEditorModel::insertCells(int index, int count)
{
    change([&](std::vector<IRealCell>& cells) {
        index = std::clamp(index, 0, int(cells.size()));
        cells.insert(cells.begin() + index, size_t(std::max(0, count)), IRealCell());
    });
}

void ChordChartEditorModel::deleteCells(int index, int count)
{
    change([&](std::vector<IRealCell>& cells) {
        if (index < 0 || index >= int(cells.size())) {
            return;
        }
        // a closing bar line on the last cell taken out stays, on the cell before
        const int end = std::min(int(cells.size()), index + count);
        QString right;
        for (int i = index; i < end; ++i) {
            if (!cells[size_t(i)].right.isEmpty()) {
                right = cells[size_t(i)].right;
            }
        }
        cells.erase(cells.begin() + index, cells.begin() + end);
        if (!right.isEmpty() && index > 0 && cells[size_t(index - 1)].right.isEmpty()) {
            cells[size_t(index - 1)].right = right;
        }
    });
    select(std::min(m_selected, int(m_cells.size()) - 1));
}

void ChordChartEditorModel::insertLine(int row)
{
    insertCells(std::clamp(row, 0, rowCount()) * 16, 16);
}

void ChordChartEditorModel::deleteLine(int row)
{
    if (rowCount() <= 1) {
        return;
    }
    deleteCells(row * 16, 16);
}

void ChordChartEditorModel::undo()
{
    if (m_undo.empty()) {
        return;
    }
    m_redo.push_back(m_cells);
    m_cells = m_undo.back();
    m_undo.pop_back();
    store();
    emit cellsChanged();
    emit selectedChanged();
}

void ChordChartEditorModel::redo()
{
    if (m_redo.empty()) {
        return;
    }
    m_undo.push_back(m_cells);
    m_cells = m_redo.back();
    m_redo.pop_back();
    store();
    emit cellsChanged();
    emit selectedChanged();
}

void ChordChartEditorModel::startAgain()
{
    change([&](std::vector<IRealCell>& cells) {
        cells = ireal::parse(m_generated.join(QString()));
    });
    m_stale = false;
    if (starScore()) {
        starScore()->setIRealEdit(QStringList());
    }
    m_edited = false;
    emit cellsChanged();
}

void ChordChartEditorModel::openInIReal()
{
    if (!starScore()) {
        return;
    }
    const QString html = starScore()->irealChartPage(ireal::serialize(m_cells));
    if (html.isEmpty()) {
        return;
    }
    QString name = m_title.isEmpty() ? QString("Chord chart") : m_title;
    name.replace(QRegularExpression("[/:\\\\]"), "-");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StarScore iReal Pro";
    QDir().mkpath(dir);
    const QString path = dir + "/" + name + " - iReal Pro.html";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(html.toUtf8());
        f.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

QVariantMap ChordChartEditorModel::preview(const QString& typed) const
{
    const QString chord = ireal::chordFromTyped(typed);
    return { { "ireal", chord }, { "display", ireal::chordDisplay(chord).join(QString()) }, { "known", ireal::knownQuality(chord) },
             { "ok", typed.trimmed().isEmpty() || !chord.isEmpty() } };
}

void ChordChartEditorModel::saveWindowSize(int width, int height)
{
    if (width > 0 && height > 0) {
        QSettings st;
        st.setValue("StarScore/chordChart/windowWidth", width);
        st.setValue("StarScore/chordChart/windowHeight", height);
    }
}

int ChordChartEditorModel::parts() const
{
    int lines = 0;
    for (const QString& l : ireal::serialize(m_cells)) {
        if (!l.trimmed().isEmpty()) {
            ++lines;
        }
    }
    return std::max(1, (lines + 11) / 12);
}

QVariantMap ChordChartEditorModel::cellMap(int index) const
{
    QVariantMap m;
    if (index < 0 || index >= int(m_cells.size())) {
        return m;
    }
    static const QRegularExpression count("^(\\d+)x$");
    const IRealCell& c = m_cells[size_t(index)];
    const QStringList d = ireal::chordDisplay(c.content);
    m["index"] = index;
    m["content"] = c.content;
    m["typed"] = ireal::typedFromIReal(c.content);
    m["isChord"] = !c.content.isEmpty() && !QString("xrnp").contains(c.content);
    m["root"] = d.value(0);
    m["quality"] = d.value(1);
    m["bass"] = d.value(2);
    m["known"] = ireal::knownQuality(c.content);
    m["alt"] = ireal::chordDisplay(c.alt).join(QString());
    m["altTyped"] = ireal::typedFromIReal(c.alt);
    m["small"] = c.small;
    m["left"] = c.left;
    m["right"] = c.right;
    m["mark"] = c.mark;
    m["tsCode"] = c.ts;
    QString ts;
    if (c.ts == "T12") {
        ts = "12/8";
    } else if (c.ts.size() == 3) {
        ts = c.ts.mid(1, 1) + "/" + c.ts.mid(2, 1);
    }
    m["ts"] = ts;
    m["ending"] = c.ending;
    QString above, below;
    int times = 0;
    for (const QString& t : c.texts) {
        const QRegularExpressionMatch cm = count.match(t);
        if (cm.hasMatch()) {
            times = cm.captured(1).toInt();
        } else if (t.startsWith('*')) {
            above = t.mid(t.size() > 3 && t[1].isDigit() && t[2].isDigit() ? 3 : 1);
        } else {
            below += (below.isEmpty() ? "" : " ") + t;
        }
    }
    m["above"] = above;
    m["below"] = below;
    m["times"] = times;
    m["segno"] = c.symbols.contains('S');
    m["coda"] = c.symbols.contains('Q');
    m["fermata"] = c.symbols.contains('f');
    m["row"] = index / 16;
    m["col"] = index % 16;
    return m;
}

QVariantList ChordChartEditorModel::cells() const
{
    QVariantList l;
    for (int i = 0; i < int(m_cells.size()); ++i) {
        l << cellMap(i);
    }
    return l;
}

QVariantMap ChordChartEditorModel::current() const
{
    return cellMap(m_selected);
}
