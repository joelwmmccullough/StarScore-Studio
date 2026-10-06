/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the status bar's "Show Flexible horns as" menu
 */
#include "flexibleviewmodel.h"

#include <QVariantMap>

#include "translation.h"

using namespace mu::project;

namespace {
struct ViewChoice {
    const char* title;   // in the button and the menu
    const char* detail;  // in the menu: what each chair shows as (2-Horn; 3-Horn)
};

const std::vector<ViewChoice> CHOICES {
    { "Reference instruments", "B♭ Trumpet, Tenor Sax; B♭ Trumpet, Alto Sax, Tenor Sax" },
    { "Range-optimized clefs", "Treble, alto; treble, soprano, alto" },
    { "Standard clefs", "Treble, bass; treble, treble, bass" },
    { "Treble clefs", "Treble, treble 8vb; treble, treble, treble 8vb" },
    { "Bass clefs", "Bass 8va, bass; bass 8va, bass 8va, bass" },
    { "B♭ instruments", "B♭ Trumpet, Tenor Sax; B♭ Trumpet, B♭ Trumpet, Tenor Sax" },
    { "E♭ instruments", "Alto Sax or E♭ Trumpet, Bari Sax; the same with two on top" },
};
}

FlexibleViewModel::FlexibleViewModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void FlexibleViewModel::load()
{
    starScore()->changed().onNotify(this, [this]() { emit changed(); });
    emit changed();
}

void FlexibleViewModel::choose(const QString& itemId)
{
    bool ok = false;
    const int mode = itemId.toInt(&ok);
    if (ok && mode != current()) {
        starScore()->setFlexibleViewMode(mode);
        emit changed();
    }
}

bool FlexibleViewModel::available() const
{
    return starScore()->hasFlexibleSections();
}

int FlexibleViewModel::current() const
{
    return starScore()->flexibleViewMode();
}

QString FlexibleViewModel::currentTitle() const
{
    const int c = current();
    return c >= 0 && c < int(CHOICES.size()) ? muse::qtrc("starscore", CHOICES[size_t(c)].title) : QString();
}

QVariantList FlexibleViewModel::menuItems() const
{
    QVariantList items;
    const int c = current();
    for (int i = 0; i < int(CHOICES.size()); ++i) {
        items << QVariantMap {
            { "id", QString::number(i) },
            { "title", QString("%1 (%2)").arg(muse::qtrc("starscore", CHOICES[size_t(i)].title),
                                              muse::qtrc("starscore", CHOICES[size_t(i)].detail)) },
            { "checkable", true }, { "checked", i == c }, { "enabled", true },
        };
    }
    return items;
}
