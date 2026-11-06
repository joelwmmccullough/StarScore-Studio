/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the status bar's "Note colors" toggle (1.27.4, Joel 5 Nov 2026: "make 'see note colors' be a
 * toggle on the bottom, similar to the 'concert pitch' toggle … make notehead colors visible just like on the
 * exported solos"). The note heads drawn in the solo sheets' 12 pitch colours while writing. One setting for every
 * score, kept on this computer; on screen only (the file and the exported sheets don't change).
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace mu::project {
class NoteColorsViewModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(bool on READ on NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };

public:
    explicit NoteColorsViewModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void toggle();

    bool on() const;

    //! the setting as saved (read at startup too)
    static bool saved();

signals:
    void changed();
};
}
