/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Reference PDF panel beside the score
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class ReferencePanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList references READ references NOTIFY changed)   // [{ id, name }]
    Q_PROPERTY(QString currentId READ currentId NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(bool invert READ invert NOTIFY changed)   // current PDF shown with inverted colours
    Q_PROPERTY(QVariantList instrumentChoices READ instrumentChoices NOTIFY changed)   // [{ text, value }]
    //! Goes up each time a page drawn in the background is ready (pageUrl then gives it)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit ReferencePanelModel(QObject* parent = nullptr);

    QVariantList references() const;
    QString currentId() const;
    int currentIndex() const;
    int pageCount() const;
    bool invert() const;
    QVariantList instrumentChoices() const;
    int revision() const { return m_revision; }

    // Managing the list
    Q_INVOKABLE void selectId(const QString& id);
    Q_INVOKABLE void rename(const QString& id, const QString& name);
    Q_INVOKABLE void setInstrument(const QString& id, const QString& instrument);
    Q_INVOKABLE void move(const QString& id, int delta);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void setInvert(bool invert);

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectIndex(int index);
    //! file:// URL of a page drawn widthPx wide. Not drawn at that width yet: it is drawn in the background (revision
    //! goes up when it's ready) and meanwhile the page as last shown is given, or nothing
    Q_INVOKABLE QUrl pageUrl(int page, int widthPx) const;
    Q_INVOKABLE void addReference();
    Q_INVOKABLE void openInViewer();

signals:
    void changed();
    void revisionChanged();

private:
    void refresh();

    QString m_currentId;
    int m_pageCount = 0;
    int m_revision = 0;
    mutable QSet<QString> m_drawing;                // PNGs being drawn in the background
    mutable QSet<QString> m_failed;                 // PNGs that couldn't be drawn (not tried again)
    mutable QHash<QString, QString> m_lastShown;   // "<reference id>/<page>" -> the PNG shown last
};
}
