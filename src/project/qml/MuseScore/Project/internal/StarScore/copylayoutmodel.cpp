/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "copylayoutmodel.h"

#include "translation.h"

#include "notation/imasternotation.h"
#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "project/internal/starscore/starscoreengraving.h"

using namespace mu::project;
using namespace mu::notation;

CopyLayoutModel::CopyLayoutModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void CopyLayoutModel::load(const QString& mode)
{
    m_fromMode = mode == "from";
    m_entries.clear();

    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master) {
        emit loaded();
        emit targetsChanged();
        return;
    }

    m_entries.push_back({ muse::qtrc("starscore", "Main score"), master->notation(), false });
    for (const IExcerptNotationPtr& excerpt : master->excerpts()) {
        if (excerpt->notation() && excerpt->notation()->elements() && excerpt->notation()->elements()->msScore()) {
            m_entries.push_back({ excerpt->name(), excerpt->notation(), true });
        }
    }

    int currentIndex = 0;
    INotationPtr current = globalContext()->currentNotation();
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].notation == current) {
            currentIndex = int(i);
        }
    }

    m_currentIndex = currentIndex;
    if (m_fromMode) {
        // Copy into the part being viewed, from the first other part book (or the main score)
        for (Entry& e : m_entries) {
            e.checked = false;
        }
        m_entries[currentIndex].checked = true;
        m_sourceIndex = currentIndex == 0 ? (m_entries.size() > 1 ? 1 : 0) : 1;
        if (m_sourceIndex == currentIndex) {
            m_sourceIndex = 0;
        }
    } else {
        // The part being viewed is the source; every other part is ticked
        m_sourceIndex = currentIndex;
        m_entries[m_sourceIndex].checked = false;
    }

    emit loaded();
    emit sourceIndexChanged();
    emit targetsChanged();
}

QVariantList CopyLayoutModel::sources() const
{
    QVariantList list;
    for (const Entry& e : m_entries) {
        list << e.title;
    }
    return list;
}

int CopyLayoutModel::sourceIndex() const
{
    return m_sourceIndex;
}

void CopyLayoutModel::setSourceIndex(int index)
{
    if (index < 0 || index >= int(m_entries.size()) || index == m_sourceIndex) {
        return;
    }
    if (m_fromMode && index == m_currentIndex) {
        emit sourceIndexChanged();   // the part being viewed can't copy from itself: keep the previous choice
        return;
    }
    m_sourceIndex = index;
    m_entries[index].checked = false;   // a part can't be both the source and a target
    emit sourceIndexChanged();
    emit targetsChanged();
}

QVariantList CopyLayoutModel::targets() const
{
    QVariantList list;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        list << QVariantMap {
            { "title", m_entries[i].title },
            { "checked", m_entries[i].checked && int(i) != m_sourceIndex },
            { "enabled", int(i) != m_sourceIndex }
        };
    }
    return list;
}

void CopyLayoutModel::setTargetChecked(int index, bool checked)
{
    if (m_fromMode || index < 0 || index >= int(m_entries.size())) {
        return;
    }
    m_entries[index].checked = checked;
    emit targetsChanged();
}

void CopyLayoutModel::setAllTargets(bool checked)
{
    if (m_fromMode) {
        return;
    }
    for (size_t i = 0; i < m_entries.size(); ++i) {
        m_entries[i].checked = checked && int(i) != m_sourceIndex;
    }
    emit targetsChanged();
}

QString CopyLayoutModel::apply()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || m_entries.empty()) {
        return QString();
    }

    const engraving::Score* source = m_entries[m_sourceIndex].notation->elements()->msScore();

    std::vector<engraving::Score*> targets;
    std::vector<INotationPtr> targetNotations;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (int(i) != m_sourceIndex && m_entries[i].checked) {
            targets.push_back(m_entries[i].notation->elements()->msScore());
            targetNotations.push_back(m_entries[i].notation);
        }
    }
    if (targets.empty()) {
        return muse::qtrc("starscore", "Nothing selected.");
    }

    starscore::LayoutCopyOptions options;
    options.lineBreaks = m_lineBreaks;
    options.pageBreaks = m_pageBreaks;
    options.keepTogether = m_keepTogether;
    options.systemLocks = m_systemLocks;
    options.replaceExisting = m_replaceExisting;

    INotationUndoStackPtr undoStack = master->notation()->undoStack();
    undoStack->prepareChanges(muse::TranslatableString::untranslatable("Copy layout breaks"));
    const starscore::LayoutCopyResult result = starscore::copyLayout(source, targets, options);
    undoStack->commitChanges();

    for (const INotationPtr& n : targetNotations) {
        n->notationChanged().notify();
    }

    return muse::qtrc("starscore", "Updated %1 of %2 scores: %3 breaks added, %4 removed, %5 system locks copied.")
           .arg(result.scoresChanged).arg(targets.size()).arg(result.breaksAdded).arg(result.breaksRemoved).arg(result.locksCopied);
}
