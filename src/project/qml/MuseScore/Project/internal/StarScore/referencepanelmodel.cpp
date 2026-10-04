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
        list << QVariantMap { { "id", r.id }, { "text", r.name }, { "value", r.id }, { "name", r.name },
                              { "instrument", r.instrument } };
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

QVariantList ReferencePanelModel::instrumentChoices() const
{
    QVariantList list;
    list << QVariantMap { { "text", muse::qtrc("starscore", "No instrument") }, { "value", QString() } };
    QStringList names = starScore()->referenceInstrumentChoices();
    // tags already used stay selectable even if the instrument is no longer in the score
    for (const StarScoreReference& r : starScore()->references()) {
        if (!r.instrument.isEmpty() && !names.contains(r.instrument, Qt::CaseInsensitive)) {
            names << r.instrument;
        }
    }
    for (const QString& n : names) {
        list << QVariantMap { { "text", n }, { "value", n } };
    }
    return list;
}

void ReferencePanelModel::selectId(const QString& id)
{
    starScore()->setCurrentReferenceId(id);
    refresh();
}

void ReferencePanelModel::rename(const QString& id, const QString& name)
{
    starScore()->renameReference(id, name);
}

void ReferencePanelModel::setInstrument(const QString& id, const QString& instrument)
{
    starScore()->setReferenceInstrument(id, instrument);
}

void ReferencePanelModel::move(const QString& id, int delta)
{
    const std::vector<StarScoreReference> refs = starScore()->references();
    for (size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].id == id) {
            starScore()->moveReference(id, int(i) + delta);
            return;
        }
    }
}

void ReferencePanelModel::remove(const QString& id)
{
    QString name;
    for (const StarScoreReference& r : starScore()->references()) {
        if (r.id == id) {
            name = r.name;
        }
    }
    constexpr int Remove = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
    constexpr int Keep = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
    const muse::IInteractive::Result answer = interactive()->questionSync(
        muse::trc("starscore", "Delete this reference PDF?"),
        muse::qtrc("starscore", "“%1” will be removed from this score when you save. The original file on your computer is not touched.")
        .arg(name).toStdString(), {
        muse::IInteractive::ButtonData(Keep, muse::trc("starscore", "Cancel"), true),
        muse::IInteractive::ButtonData(Remove, muse::trc("starscore", "Delete")),
    }, Keep);
    if (answer.button() == Remove) {
        starScore()->removeReference(id);
    }
}

bool ReferencePanelModel::invert() const
{
    for (const StarScoreReference& r : starScore()->references()) {
        if (r.id == m_currentId) {
            return r.invert;
        }
    }
    return true;
}

void ReferencePanelModel::setInvert(bool invert)
{
    if (!m_currentId.isEmpty()) {
        starScore()->setReferenceInvert(m_currentId, invert);   // saved in the .starscore; notifies changed
    }
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
    // The same pick-files / "already here?" / addReference loop is in StarScoreBarModel ("ref-add",
    // starscorebarmodel.cpp). It belongs in one place (StarScoreService, say, with the question asked through a
    // callback); left duplicated for now so the two can be changed together.
    const QFileInfo mainFile(starScore()->mainProjectPath().toQString());
    const muse::io::paths_t paths = interactive()->selectOpeningFilesSync(
        muse::trc("starscore", "Add reference PDFs"), muse::io::path_t(mainFile.absolutePath()), { "PDF (*.pdf)" });
    QString last;
    for (const muse::io::path_t& p : paths) {
        const QString same = starScore()->identicalReferenceName(p);
        if (!same.isEmpty()) {
            constexpr int Add = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
            constexpr int Skip = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
            const muse::IInteractive::Result answer = interactive()->questionSync(
                muse::trc("starscore", "This PDF is already here"),
                muse::qtrc("starscore", "“%1” is identical to the reference PDF “%2” already in this score. Import it anyway?")
                .arg(QFileInfo(p.toQString()).fileName(), same).toStdString(), {
                muse::IInteractive::ButtonData(Skip, muse::trc("starscore", "Don't import"), true),
                muse::IInteractive::ButtonData(Add, muse::trc("starscore", "Import anyway")),
            }, Skip);
            if (answer.button() != Add) {
                continue;
            }
        }
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
