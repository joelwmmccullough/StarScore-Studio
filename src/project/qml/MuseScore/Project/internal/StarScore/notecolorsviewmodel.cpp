/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the status bar's "Note colors" toggle (see notecolorsviewmodel.h)
 */
#include "notecolorsviewmodel.h"

#include <QSettings>

#include "engraving/dom/mscore.h"

using namespace mu::project;

static const char* SETTING = "StarScore/view/noteColors";

NoteColorsViewModel::NoteColorsViewModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

bool NoteColorsViewModel::saved()
{
    return QSettings().value(SETTING, false).toBool();
}

void NoteColorsViewModel::load()
{
    const bool s = saved();
    if (s != mu::engraving::MScore::starscoreViewNoteColors) {
        mu::engraving::MScore::starscoreViewNoteColors = s;
        if (globalContext() && globalContext()->currentNotation()) {
            globalContext()->currentNotation()->notationChanged().notify();
        }
    }
    emit changed();
}

void NoteColorsViewModel::toggle()
{
    const bool now = !mu::engraving::MScore::starscoreViewNoteColors;
    mu::engraving::MScore::starscoreViewNoteColors = now;
    QSettings().setValue(SETTING, now);
    if (globalContext() && globalContext()->currentNotation()) {
        globalContext()->currentNotation()->notationChanged().notify();   // (draws the score again)
    }
    emit changed();
}

bool NoteColorsViewModel::on() const
{
    return mu::engraving::MScore::starscoreViewNoteColors;
}
