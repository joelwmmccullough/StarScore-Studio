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
#include "projectuiactions.h"

#include "modularity/ioc.h"
#include "types/translatablestring.h"
#include "context/shortcutcontext.h"

using namespace mu::project;
using namespace muse;
using namespace muse::ui;
using namespace muse::actions;

const UiActionList ProjectUiActions::m_actions = {
    UiAction("file-open",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Open…"),
             TranslatableString("action", "Open"),
             IconCode::Code::OPEN_FILE
             ),
    UiAction("file-new",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&New StarScore…"),
             TranslatableString("action", "New StarScore"),
             IconCode::Code::NEW_FILE
             ),
    UiAction("file-new-musescore",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "New score (MuseScore wizard)…"),
             TranslatableString("action", "New score with the MuseScore wizard")
             ),
    UiAction("starscore-copy-layout",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Copy layout breaks to other parts…"),
             TranslatableString("action", "Copy layout breaks to other parts")
             ),
    UiAction("starscore-part-styles",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Part styles…"),
             TranslatableString("action", "Part styles")
             ),
    UiAction("starscore-additive-timesig",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Additive time signature…"),
             TranslatableString("action", "Additive time signature (e.g. 4+4+4+3/8)")
             ),
    UiAction("starscore-add-solo",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Add solo transcription…"),
             TranslatableString("action", "Add solo transcription")
             ),
    UiAction("starscore-export-band",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Export to Sheets and Demos…"),
             TranslatableString("action", "Export every sheet to the band's Sheets and Demos folder")
             ),
    UiAction("starscore-compare-parts",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Compare parts…"),
             TranslatableString("action", "See which bars differ between parts on the same instrument")
             ),
    UiAction("starscore-toggle-minmaj",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Minor-major symbol in this part on/off"),
             TranslatableString("action", "Switch the minor-major seventh triangle for the part being viewed")
             ),
    UiAction("starscore-voice-order",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Check voice order…"),
             TranslatableString("action", "Flag bars where parts of a section cross or double")
             ),
    UiAction("starscore-check-ranges",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Check instrument ranges"),
             TranslatableString("action", "List the bars where visible instruments go outside their ranges")
             ),
    UiAction("starscore-color-notes",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Color notes by pitch"),
             TranslatableString("action", "Color notes by pitch (selection, or whole score)")
             ),
    UiAction("starscore-uncolor-notes",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Remove note colors"),
             TranslatableString("action", "Remove note colors (selection, or whole score)")
             ),
    UiAction("file-close",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Close"),
             TranslatableString("action", "Close")
             ),
    UiAction("file-save",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Save"),
             TranslatableString("action", "Save"),
             IconCode::Code::SAVE
             ),
    UiAction("file-save-as",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Save &as…"),
             TranslatableString("action", "Save as")
             ),
    UiAction("file-save-a-copy",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Save a &copy…"),
             TranslatableString("action", "Save a copy")
             ),
    UiAction("file-save-selection",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Save &selection…"),
             TranslatableString("action", "Save selection")
             ),
    UiAction("file-save-to-cloud",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Save to clo&ud…"),
             TranslatableString("action", "Save to cloud"),
             IconCode::Code::CLOUD_FILE
             ),
    UiAction("file-publish",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Publish to &MuseScore.com…"),
             TranslatableString("action", "Publish to MuseScore.com"),
             IconCode::Code::CLOUD_FILE
             ),
    UiAction("file-share-audio",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Share on &Audio.com…"),
             TranslatableString("action", "Share on Audio.com"),
             IconCode::Code::SHARE_AUDIO
             ),
    UiAction("file-export",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Export…"),
             TranslatableString("action", "Export"),
             IconCode::Code::SHARE_FILE
             ),
    UiAction("file-import-pdf",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Import P&DF…"),
             TranslatableString("action", "Import PDF"),
             IconCode::Code::OPEN_LINK
             ),
    UiAction("file-import-audio-to-score",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Import A&udio to Score…"),
             TranslatableString("action", "Import Audio to Score"),
             IconCode::Code::OPEN_LINK
             ),
    UiAction("project-properties",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Project propert&ies…"),
             TranslatableString("action", "Project properties")
             ),
    UiAction("print",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Print…"),
             TranslatableString("action", "Print"),
             IconCode::Code::PRINT
             ),
    UiAction("clear-recent",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "&Clear list of recent files"),
             TranslatableString("action", "Clear list of recent files")
             )
};

ProjectUiActions::ProjectUiActions(std::shared_ptr<ProjectActionsController> controller, const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_controller(controller)
{
}

const UiActionList& ProjectUiActions::actionsList() const
{
    return m_actions;
}

bool ProjectUiActions::actionEnabled(const UiAction& act) const
{
    if (!m_controller->canReceiveAction(act.code)) {
        return false;
    }

    return true;
}

bool ProjectUiActions::actionChecked(const UiAction&) const
{
    return false;
}

muse::async::Channel<ActionCodeList> ProjectUiActions::actionEnabledChanged() const
{
    return m_actionEnabledChanged;
}

muse::async::Channel<ActionCodeList> ProjectUiActions::actionCheckedChanged() const
{
    return m_actionCheckedChanged;
}
