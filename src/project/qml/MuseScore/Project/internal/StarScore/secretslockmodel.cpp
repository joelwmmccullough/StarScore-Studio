/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "secretslockmodel.h"

#include <QCryptographicHash>
#include <QSettings>
#include <QUuid>

#include "translation.h"

using namespace mu::project;

static const char* SETTING_HASH = "StarScore/secretsHash";
static const char* SETTING_SALT = "StarScore/secretsSalt";

//! Unlocked for the rest of this run of StarScore (every Home page shares it)
static bool s_unlocked = false;

static QString hashOf(const QString& salt, const QString& password)
{
    return QString::fromLatin1(QCryptographicHash::hash((salt + "|" + password).toUtf8(), QCryptographicHash::Sha256).toHex());
}

SecretsLockModel::SecretsLockModel(QObject* parent)
    : QObject(parent)
{
}

bool SecretsLockModel::hasPassword() const
{
    return !QSettings().value(SETTING_HASH).toString().isEmpty();
}

bool SecretsLockModel::unlocked() const
{
    return s_unlocked;
}

QString SecretsLockModel::setPassword(const QString& password, const QString& again)
{
    if (hasPassword()) {
        return muse::qtrc("starscore", "A password is already set.");
    }
    if (password.size() < 4) {
        return muse::qtrc("starscore", "Use at least 4 characters.");
    }
    if (password != again) {
        return muse::qtrc("starscore", "The two passwords are different.");
    }
    const QString salt = QUuid::createUuid().toString(QUuid::Id128);
    QSettings settings;
    settings.setValue(SETTING_SALT, salt);
    settings.setValue(SETTING_HASH, hashOf(salt, password));
    s_unlocked = true;
    emit changed();
    return QString();
}

bool SecretsLockModel::unlock(const QString& password)
{
    QSettings settings;
    const QString salt = settings.value(SETTING_SALT).toString();
    const bool ok = !salt.isEmpty() && hashOf(salt, password) == settings.value(SETTING_HASH).toString();
    if (ok) {
        s_unlocked = true;
        emit changed();
    }
    return ok;
}

void SecretsLockModel::lock()
{
    s_unlocked = false;
    emit changed();
}
