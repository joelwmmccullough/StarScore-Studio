/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace mu::project {
//! "Additive time signature": e.g. 4+4+4+3 over 8 = bars of 4/8, 4/8, 4/8, 3/8, repeating.
class AdditiveTimeSigModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString rangeText READ rangeText CONSTANT)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit AdditiveTimeSigModel(QObject* parent = nullptr);

    QString rangeText() const;

    //! Returns an error message, or an empty string on success
    Q_INVOKABLE QString apply(const QString& numerators, int denominator);
};
}
