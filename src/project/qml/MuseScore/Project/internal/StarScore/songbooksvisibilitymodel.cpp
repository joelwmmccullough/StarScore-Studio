/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "songbooksvisibilitymodel.h"

#include "settings.h"

using namespace mu::project;
using namespace muse;

static const Settings::Key SHOW_SONGBOOKS("project", "starscore/showSongbooks");

SongbooksVisibilityModel::SongbooksVisibilityModel(QObject* parent)
    : QObject(parent)
{
    settings()->setDefaultValue(SHOW_SONGBOOKS, Val(false));
    settings()->valueChanged(SHOW_SONGBOOKS).onReceive(this, [this](const Val&) {
        emit shownChanged();
    });
}

bool SongbooksVisibilityModel::shown() const
{
    return settings()->value(SHOW_SONGBOOKS).toBool();
}

void SongbooksVisibilityModel::setShown(bool shown)
{
    if (shown == this->shown()) {
        return;
    }
    settings()->setSharedValue(SHOW_SONGBOOKS, Val(shown));
    emit shownChanged();
}
