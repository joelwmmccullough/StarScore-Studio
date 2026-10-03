/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "notationpagemodel.h"

#include "internal/applicationuiactions.h"
#include "dockwindow/idockwindow.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QSettings>
#include <QTimer>

#include "async/async.h"

#include "log.h"

using namespace mu::appshell;
using namespace mu::notation;
using namespace muse::actions;

NotationPageModel::NotationPageModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

bool NotationPageModel::isNavigatorVisible() const
{
    return configuration()->isNotationNavigatorVisible();
}

bool NotationPageModel::isBraillePanelVisible() const
{
    return brailleConfiguration()->braillePanelEnabled();
}

void NotationPageModel::init()
{
    TRACEFUNC;

    if (m_inited) {
        return;
    }

    for (const ActionCode& actionCode : ApplicationUiActions::toggleDockActions().keys()) {
        DockName dockName = ApplicationUiActions::toggleDockActions()[actionCode];
        dispatcher()->reg(this, actionCode, [=]() { toggleDock(dockName); });
    }

    globalContext()->currentNotationChanged().onNotify(this, [this]() {
        onNotationChanged();
        scheduleUpdateDrumsetPanelVisibility();
        scheduleUpdatePercussionPanelVisibility();
    });

    extensionsProvider()->manifestListChanged().onNotify(this, [this]() {
        scheduleUpdateExtensionsToolBarVisibility();
    });

    extensionsProvider()->manifestChanged().onReceive(this, [this](const muse::extensions::Manifest&) {
        scheduleUpdateExtensionsToolBarVisibility();
    });

    brailleConfiguration()->braillePanelEnabledChanged().onNotify(this, [this]() {
        emit isBraillePanelVisibleChanged();
    });

    onNotationChanged();

    scheduleUpdateDrumsetPanelVisibility();
    scheduleUpdatePercussionPanelVisibility();
    scheduleUpdateExtensionsToolBarVisibility();

    notationSceneConfiguration()->useNewPercussionPanelChanged().onNotify(this, [this]() {
        scheduleUpdateDrumsetPanelVisibility();
        scheduleUpdatePercussionPanelVisibility();
    });

    notationSceneConfiguration()->percussionPanelAutoShowModeChanged().onNotify(this, [this]() {
        scheduleUpdatePercussionPanelVisibility();
    });

    // StarScore: a narrow window keeps the score, closing side panels (and reopening them when it widens again)
    if (muse::dock::IDockWindow* window = dockWindowProvider()->window()) {
        QObject::connect(&window->asItem(), &QQuickItem::widthChanged, this, [this]() {
            scheduleFitPanelsToWidth();
        });
    }
    scheduleFitPanelsToWidth();

    m_inited = true;
}

namespace {
//! The score keeps at least this much width before a side panel column closes
constexpr int STARSCORE_MIN_SCORE_WIDTH = 480;
//! and a closed column comes back only with this much more room, so it doesn't flicker at the boundary
constexpr int STARSCORE_PANEL_SLACK = 40;
const char* STARSCORE_AUTO_CLOSED_KEY = "starscore/autoClosedPanels";

//! The columns closed for lack of room, last closed last: [{ "names": [...], "width": n }]
QJsonArray starscoreAutoClosed()
{
    return QJsonDocument::fromJson(QSettings().value(STARSCORE_AUTO_CLOSED_KEY).toByteArray()).array();
}

void starscoreSetAutoClosed(const QJsonArray& columns)
{
    QSettings().setValue(STARSCORE_AUTO_CLOSED_KEY, QJsonDocument(columns).toJson(QJsonDocument::Compact));
}
}

void NotationPageModel::scheduleFitPanelsToWidth()
{
    if (m_fitPanelsScheduled) {
        return;
    }
    m_fitPanelsScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_fitPanelsScheduled = false;
        fitPanelsToWidth();
    });
}

void NotationPageModel::fitPanelsToWidth()
{
    muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!window) {
        return;
    }
    const QList<QPair<QString, QRect> > open = window->openSidePanels();
    // only on the score page
    if (window->currentPageUri() != "musescore://notation") {
        return;
    }
    const int windowWidth = int(window->asItem().width());
    if (windowWidth <= 0) {
        return;
    }

    // the side panel columns now open: panels shown as tabs together share one column (one frame)
    struct Column {
        QRect frame;
        QStringList names;
    };
    std::vector<Column> columns;
    int sideWidth = 0;
    for (const auto& [name, frame] : open) {
        auto it = std::find_if(columns.begin(), columns.end(), [&](const Column& c) { return c.frame == frame; });
        if (it == columns.end()) {
            columns.push_back({ frame, { name } });
            sideWidth += frame.width();
        } else {
            it->names << name;
        }
    }

    QJsonArray closed = starscoreAutoClosed();

    if (!columns.empty() && windowWidth - sideWidth < STARSCORE_MIN_SCORE_WIDTH) {
        // the right-most column closes first
        const Column& col = *std::max_element(columns.begin(), columns.end(), [](const Column& a, const Column& b) {
            return a.frame.x() < b.frame.x();
        });
        QJsonArray names;
        for (const QString& name : col.names) {
            names.append(name);
        }
        closed.append(QJsonObject { { "names", names }, { "width", col.frame.width() } });
        starscoreSetAutoClosed(closed);
        for (const QString& name : col.names) {
            dispatcher()->dispatch("dock-set-open", muse::actions::ActionData::make_arg2<QString, bool>(name, false));
        }
        scheduleFitPanelsToWidth();   // still too narrow? the next one goes too
        return;
    }

    if (!closed.isEmpty()) {
        const QJsonObject last = closed.last().toObject();
        if (windowWidth - sideWidth - last.value("width").toInt() >= STARSCORE_MIN_SCORE_WIDTH + STARSCORE_PANEL_SLACK) {
            closed.removeLast();
            starscoreSetAutoClosed(closed);
            // in their order: each panel reopened as a tab goes after the ones already back
            const QJsonArray names = last.value("names").toArray();
            for (qsizetype i = 0; i < names.size(); ++i) {
                const QString name = names.at(i).toString();
                if (!window->isDockOpen(name)) {
                    dispatcher()->dispatch("dock-set-open", muse::actions::ActionData::make_arg2<QString, bool>(name, true));
                }
            }
            scheduleFitPanelsToWidth();   // room for another one?
        }
    }
}

QString NotationPageModel::notationToolBarName() const
{
    return NOTATION_TOOLBAR_NAME;
}

QString NotationPageModel::playbackToolBarName() const
{
    return PLAYBACK_TOOLBAR_NAME;
}

QString NotationPageModel::undoRedoToolBarName() const
{
    return UNDO_REDO_TOOLBAR_NAME;
}

QString NotationPageModel::noteInputBarName() const
{
    return NOTE_INPUT_BAR_NAME;
}

QString NotationPageModel::extensionsToolBarName() const
{
    return EXTENSIONS_TOOLBAR_NAME;
}

QString NotationPageModel::palettesPanelName() const
{
    return PALETTES_PANEL_NAME;
}

QString NotationPageModel::layoutPanelName() const
{
    return LAYOUT_PANEL_NAME;
}

QString NotationPageModel::inspectorPanelName() const
{
    return INSPECTOR_PANEL_NAME;
}

QString NotationPageModel::selectionFiltersPanelName() const
{
    return SELECTION_FILTERS_PANEL_NAME;
}

QString NotationPageModel::undoHistoryPanelName() const
{
    return UNDO_HISTORY_PANEL_NAME;
}

QString NotationPageModel::starscoreReferencePanelName() const
{
    return STARSCORE_REFERENCE_PANEL_NAME;
}

QString NotationPageModel::starscoreAuditPanelName() const
{
    return STARSCORE_AUDIT_PANEL_NAME;
}

QString NotationPageModel::starscoreTodoPanelName() const
{
    return STARSCORE_TODO_PANEL_NAME;
}

QString NotationPageModel::mixerPanelName() const
{
    return MIXER_PANEL_NAME;
}

QString NotationPageModel::pianoKeyboardPanelName() const
{
    return PIANO_KEYBOARD_PANEL_NAME;
}

QString NotationPageModel::timelinePanelName() const
{
    return TIMELINE_PANEL_NAME;
}

QString NotationPageModel::drumsetPanelName() const
{
    return DRUMSET_PANEL_NAME;
}

QString NotationPageModel::percussionPanelName() const
{
    return PERCUSSION_PANEL_NAME;
}

QString NotationPageModel::statusBarName() const
{
    return NOTATION_STATUSBAR_NAME;
}

void NotationPageModel::onNotationChanged()
{
    INotationPtr notation = globalContext()->currentNotation();
    if (!notation) {
        return;
    }

    INotationNoteInputPtr noteInput = notation->interaction()->noteInput();
    noteInput->stateChanged().onNotify(this, [this]() {
        scheduleUpdateDrumsetPanelVisibility();
        scheduleUpdatePercussionPanelVisibility();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    INotationInteractionPtr notationInteraction = notation->interaction();
    notationInteraction->selectionChanged().onNotify(this, [this]() {
        scheduleUpdateDrumsetPanelVisibility();
        scheduleUpdatePercussionPanelVisibility();
    }, Asyncable::Mode::SetReplace /* FIXME */);
}

void NotationPageModel::toggleDock(const QString& name)
{
    if (name == NOTATION_NAVIGATOR_PANEL_NAME) {
        configuration()->setIsNotationNavigatorVisible(!isNavigatorVisible());
        emit isNavigatorVisibleChanged();
        return;
    }

    if (name == NOTATION_BRAILLE_PANEL_NAME) {
        brailleConfiguration()->setBraillePanelEnabled(!isBraillePanelVisible());
        emit isBraillePanelVisibleChanged();
        return;
    }

    dispatcher()->dispatch("dock-toggle", ActionData::make_arg1<QString>(name));
}

void NotationPageModel::scheduleUpdateDrumsetPanelVisibility()
{
    if (m_updateDrumsetPanelVisibilityScheduled) {
        return;
    }

    m_updateDrumsetPanelVisibilityScheduled = true;

    //! NOTE: ensure we don't update it multiple times in succession
    muse::async::Async::call(this, [this]() {
        doUpdateDrumsetPanelVisibility();
        m_updateDrumsetPanelVisibilityScheduled = false;
    });
}

void NotationPageModel::doUpdateDrumsetPanelVisibility()
{
    TRACEFUNC;

    const muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!window) {
        return;
    }

    auto setDrumsetPanelOpen = [this, window](bool open) {
        if (open == window->isDockOpen(DRUMSET_PANEL_NAME)) {
            return;
        }

        dispatcher()->dispatch("dock-set-open", ActionData::make_arg2<QString, bool>(DRUMSET_PANEL_NAME, open));
    };

    // This should never be open when the new percussion panel is in use...
    if (notationSceneConfiguration()->useNewPercussionPanel()) {
        setDrumsetPanelOpen(false);
        return;
    }

    const INotationPtr notation = globalContext()->currentNotation();
    if (!notation) {
        setDrumsetPanelOpen(false);
        return;
    }

    const INotationNoteInputPtr noteInput = notation->interaction()->noteInput();
    const bool shouldOpen = noteInput->isNoteInputMode() && noteInput->state().drumset() != nullptr;

    setDrumsetPanelOpen(shouldOpen);
}

void NotationPageModel::scheduleUpdatePercussionPanelVisibility()
{
    if (m_updatePercussionPanelVisibilityScheduled) {
        return;
    }

    m_updatePercussionPanelVisibilityScheduled = true;

    //! NOTE: ensure we don't update it multiple times in succession
    muse::async::Async::call(this, [this]() {
        doUpdatePercussionPanelVisibility();
        m_updatePercussionPanelVisibilityScheduled = false;
    });
}

void NotationPageModel::doUpdatePercussionPanelVisibility()
{
    TRACEFUNC;

    //! NOTE: If the user is entering percussion notes with the piano keyboard, we can assume that they
    //! don't want the percussion panel to auto-show...
    const muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!window || window->isDockOpen(PIANO_KEYBOARD_PANEL_NAME)) {
        return;
    }

    auto setPercussionPanelOpen = [this, window](bool open) {
        if (open == window->isDockOpen(PERCUSSION_PANEL_NAME)) {
            return;
        }

        dispatcher()->dispatch("dock-set-open", ActionData::make_arg2<QString, bool>(PERCUSSION_PANEL_NAME, open));
    };

    // This should never be open when the old drumset panel is in use...
    if (!notationSceneConfiguration()->useNewPercussionPanel()) {
        setPercussionPanelOpen(false);
        return;
    }

    const PercussionPanelAutoShowMode autoShowMode = notationSceneConfiguration()->percussionPanelAutoShowMode();
    const INotationPtr notation = globalContext()->currentNotation();
    if (!notation || !notation->elements() || autoShowMode == PercussionPanelAutoShowMode::NEVER) {
        return;
    }

    const INotationNoteInputPtr noteInput = notation->interaction()->noteInput();
    const bool autoClose = notationSceneConfiguration()->autoClosePercussionPanel();
    if (noteInput && !noteInput->isNoteInputMode() && autoShowMode == PercussionPanelAutoShowMode::UNPITCHED_STAFF_NOTE_INPUT) {
        if (autoClose) {
            setPercussionPanelOpen(false);
        }
        return;
    }

    const mu::engraving::Score* score = notation->elements()->msScore();
    const INotationSelectionPtr selection = notation->interaction()->selection();
    if (!score || !selection || selection->isNone()) {
        if (autoClose) {
            setPercussionPanelOpen(false);
        }
        return;
    }

    if (selection->isRange()) {
        const INotationSelectionRangePtr rangeSelection = selection->range();
        if (!rangeSelection) {
            if (autoClose) {
                setPercussionPanelOpen(false);
            }
            return;
        }
        for (const Part* p : rangeSelection->selectedParts()) {
            if (p->hasDrumStaff()) {
                continue;
            }
            if (autoClose) {
                setPercussionPanelOpen(false);
            }
            return;
        }
    } else {
        for (const EngravingItem* e : selection->elements()) {
            const Staff* staff = e->staff();
            if (staff && staff->isDrumStaff(e->tick())) {
                continue;
            }
            if (autoClose) {
                setPercussionPanelOpen(false);
            }
            return;
        }
    }

    setPercussionPanelOpen(true);
}

void NotationPageModel::scheduleUpdateExtensionsToolBarVisibility()
{
    if (m_updateExtensionsToolBarVisibilityScheduled) {
        return;
    }

    m_updateExtensionsToolBarVisibilityScheduled = true;

    //! NOTE: ensure we don't update it multiple times in succession
    muse::async::Async::call(this, [this]() {
        doUpdateExtensionsToolBarVisibility();
        m_updateExtensionsToolBarVisibilityScheduled = false;
    });
}

void NotationPageModel::doUpdateExtensionsToolBarVisibility()
{
    const muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!window) {
        return;
    }

    auto setExtensionsToolBarOpen = [this, window](bool open) {
        if (open == window->isDockOpen(EXTENSIONS_TOOLBAR_NAME)) {
            return;
        }

        dispatcher()->dispatch("dock-set-open", ActionData::make_arg2<QString, bool>(EXTENSIONS_TOOLBAR_NAME, open));
    };

    muse::extensions::ManifestList enabledExtensions = extensionsProvider()->manifestList(muse::extensions::Filter::Enabled);
    for (const muse::extensions::Manifest& m : enabledExtensions) {
        for (const muse::extensions::Action& a : m.actions) {
            if (!a.showOnToolbar) {
                continue;
            }

            setExtensionsToolBarOpen(true);
            return;
        }
    }

    setExtensionsToolBarOpen(false);
}
