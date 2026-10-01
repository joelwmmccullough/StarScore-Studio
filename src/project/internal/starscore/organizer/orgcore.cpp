/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: shared types and file helpers
 */
#include "orgcore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace mu::project::starscore::org {
Paths Paths::make(const QString& band, const QString& projects, const QDate& today)
{
    Paths p;
    p.band = QDir::cleanPath(band);
    p.toolkit = p.band + "/6 Inbox/.organizer";
    if (!projects.isEmpty()) {
        p.projects = QDir::cleanPath(projects);
        p.projToolkit = p.projects + "/.organizer";
    }
    p.today = today;
    return p;
}

QJsonDocument readJson(const QString& path, bool* ok)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        return QJsonDocument();
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (ok) {
        *ok = err.error == QJsonParseError::NoError;
    }
    return doc;
}

QJsonObject readJsonObject(const QString& path)
{
    return readJson(path).object();
}

bool writeText(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(data);
    return f.commit();
}

bool writeJson(const QString& path, const QJsonDocument& doc, bool compact)
{
    return writeText(path, doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
}

static QByteArray asciiJsonString(const QString& s)
{
    QByteArray out = "\"";
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += char(u);
        } else if (u == '\n') {
            out += "\\n";
        } else if (u < 0x20 || u > 0x7e) {
            out += QString("\\u%1").arg(u, 4, 16, QChar('0')).toLatin1();
        } else {
            out += char(u);
        }
    }
    return out + "\"";
}

QByteArray pythonStyleJson(const QJsonObject& o)
{
    QStringList keys = o.keys();
    keys.sort();
    QByteArray out = "{";
    bool first = true;
    for (const QString& k : keys) {
        out += first ? "\n " : ",\n ";
        first = false;
        out += asciiJsonString(k) + ": " + asciiJsonString(o.value(k).toString());
    }
    out += first ? "}" : "\n}";
    return out;
}

QString joinPath(const QString& a, const QString& b)
{
    if (a.isEmpty()) {
        return b;
    }
    if (b.isEmpty()) {
        return a;
    }
    return a.endsWith('/') ? a + b : a + "/" + b;
}

QString relativeTo(const QString& base, const QString& path)
{
    return QDir(base).relativeFilePath(path);
}

QString freeName(const QString& dst)
{
    if (!QFileInfo::exists(dst)) {
        return dst;
    }
    const QFileInfo fi(dst);
    const bool dir = fi.isDir() && !isBundle(dst);
    const QString base = dir ? fi.absoluteFilePath() : fi.absolutePath() + "/" + fi.completeBaseName();
    const QString ext = dir || fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    for (int i = 2;; ++i) {
        const QString c = QString("%1 (%2)%3").arg(base).arg(i).arg(ext);
        if (!QFileInfo::exists(c)) {
            return c;
        }
    }
}

bool moveItem(const QString& src, const QString& dst)
{
    if (QFileInfo::exists(dst) || !QFileInfo::exists(src)) {
        return false;
    }
    QDir().mkpath(QFileInfo(dst).absolutePath());
    return QDir().rename(src, dst);
}

bool isAudio(const QString& fileName)
{
    static const QStringList AUDIO { "wav", "mp3", "mid", "m4a", "aif", "aiff", "logicx" };
    return AUDIO.contains(QFileInfo(fileName).suffix().toLower());
}

bool isBundle(const QString& path)
{
    static const QStringList BUNDLES { "logicx", "mscbackup", "band", "app", "pages", "numbers", "key" };
    const QString name = QFileInfo(path).fileName().toLower();
    return name == ".mscbackup" || BUNDLES.contains(QFileInfo(path).suffix().toLower());
}

QString md5OfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}
}
