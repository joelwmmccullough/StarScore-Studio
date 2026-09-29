/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Reference PDF panel beside the score
 */
#include "referencepanelmodel.h"

#include <QFileInfo>

#include "translation.h"

using namespace mu::project;

ReferencePanelModel::ReferencePanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ReferencePanelModel::load()
{
    starScore()->changed().onNotify(this, [this]() {
        refresh();
    });
    refresh();
}

void ReferencePanelModel::refresh()
{
    m_currentId = starScore()->currentReferenceId();
    m_pageCount = m_currentId.isEmpty() ? 0 : starScore()->referencePageCount(m_currentId);
    emit changed();
}

QVariantList ReferencePanelModel::references() const
{
    QVariantList list;
    for (const StarScoreReference& r : starScore()->references()) {
        list << QVariantMap { { "id", r.id }, { "text", r.name }, { "value", r.id } };
    }
    return list;
}

QString ReferencePanelModel::currentId() const
{
    return m_currentId;
}

int ReferencePanelModel::currentIndex() const
{
    int i = 0;
    for (const StarScoreReference& r : starScore()->references()) {
        if (r.id == m_currentId) {
            return i;
        }
        ++i;
    }
    return -1;
}

int ReferencePanelModel::pageCount() const
{
    return m_pageCount;
}

void ReferencePanelModel::selectIndex(int index)
{
    const std::vector<StarScoreReference> refs = starScore()->references();
    if (index >= 0 && index < int(refs.size())) {
        starScore()->setCurrentReferenceId(refs[size_t(index)].id);
        m_pageCount = 0;
        refresh();
    }
}

QUrl ReferencePanelModel::pageUrl(int page, int widthPx) const
{
    if (m_currentId.isEmpty() || page < 0 || page >= m_pageCount) {
        return QUrl();
    }
    const QString png = starScore()->referencePageImage(m_currentId, page, widthPx);
    return png.isEmpty() ? QUrl() : QUrl::fromLocalFile(png);
}

void ReferencePanelModel::addReference()
{
    const QFileInfo mainFile(starScore()->mainProjectPath().toQString());
    const muse::io::paths_t paths = interactive()->selectOpeningFilesSync(
        muse::trc("starscore", "Add reference PDFs"), muse::io::path_t(mainFile.absolutePath()), { "PDF (*.pdf)" });
    QString last;
    for (const muse::io::path_t& p : paths) {
        muse::RetVal<QString> ret = starScore()->addReference(p);
        if (ret.ret) {
            last = ret.val;
        }
    }
    if (!last.isEmpty()) {
        starScore()->setCurrentReferenceId(last);
        m_pageCount = 0;
        refresh();
    }
}

void ReferencePanelModel::openInViewer()
{
    if (!m_currentId.isEmpty()) {
        interactive()->openUrl(QUrl::fromLocalFile(starScore()->referencePath(m_currentId).toQString()));
    }
}
