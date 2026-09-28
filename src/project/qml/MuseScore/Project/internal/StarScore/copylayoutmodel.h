/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "notation/inotation.h"

namespace mu::project {
//! "Copy layout breaks to other parts" — copies line breaks, page breaks, "keep together"
//! markers and system locks from one score/part book to others.
class CopyLayoutModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList sources READ sources NOTIFY loaded)
    Q_PROPERTY(int sourceIndex READ sourceIndex WRITE setSourceIndex NOTIFY sourceIndexChanged)
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)

    Q_PROPERTY(bool lineBreaks MEMBER m_lineBreaks NOTIFY optionsChanged)
    Q_PROPERTY(bool pageBreaks MEMBER m_pageBreaks NOTIFY optionsChanged)
    Q_PROPERTY(bool keepTogether MEMBER m_keepTogether NOTIFY optionsChanged)
    Q_PROPERTY(bool systemLocks MEMBER m_systemLocks NOTIFY optionsChanged)
    Q_PROPERTY(bool replaceExisting MEMBER m_replaceExisting NOTIFY optionsChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit CopyLayoutModel(QObject* parent = nullptr);

    QVariantList sources() const;
    int sourceIndex() const;
    void setSourceIndex(int index);
    QVariantList targets() const;

    //! mode "to": the part being viewed is the source, the others are ticked;
    //! mode "from": the part being viewed is the only target, another part is the source
    Q_INVOKABLE void load(const QString& mode = QString());
    Q_INVOKABLE void setTargetChecked(int index, bool checked);
    Q_INVOKABLE void setAllTargets(bool checked);
    //! Returns a one-line summary of what changed
    Q_INVOKABLE QString apply();

signals:
    void loaded();
    void sourceIndexChanged();
    void targetsChanged();
    void optionsChanged();

private:
    struct Entry {
        QString title;
        notation::INotationPtr notation;
        bool checked = true;
    };

    std::vector<Entry> m_entries;   // 0 = main score, then part books
    int m_sourceIndex = 0;
    bool m_fromMode = false;

    bool m_lineBreaks = true;
    bool m_pageBreaks = true;
    bool m_keepTogether = true;
    bool m_systemLocks = true;
    bool m_replaceExisting = true;
};
}
