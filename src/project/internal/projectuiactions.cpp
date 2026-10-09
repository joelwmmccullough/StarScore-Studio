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
    UiAction("starscore-layout-from",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Apply system formatting from another part…"),
             TranslatableString("action", "Copy system breaks, page breaks and system locks from another part into this one")
             ),
    UiAction("starscore-layout-to",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Copy part formatting…"),
             TranslatableString("action", "Copy one part's system breaks, page breaks and system locks to other parts")
             ),
    UiAction("starscore-part-styles",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Apply default style settings…"),
             TranslatableString("action", "Apply default style settings")
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
    UiAction("starscore-export-arrangements",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Export arrangements as MuseScore files…"),
             TranslatableString("action", "Export each arrangement as its own MuseScore file (.mscz)")
             ),
    UiAction("starscore-organize",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Run folder organization…"),
             TranslatableString("action", "Keep Sheets and Demos and Projects and Sheets in order and rebuild the PDFs that are out of date")
             ),
    UiAction("starscore-rebuild-all",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Rebuild every generated PDF…"),
             TranslatableString("action", "Organize the folders and rebuild every generated PDF in Sheets and Demos and Projects and Sheets")
             ),
    UiAction("starscore-band-roster",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Band roster…"),
             TranslatableString("action", "Who plays what, for the changelogs and Horn Part Guides")
             ),
    UiAction("starscore-recordings",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Recordings…"),
             TranslatableString("action", "This song's recordings: shows, albums, sessions, and your star ratings")
             ),
    UiAction("starscore-audit-library",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Audit library…"),
             TranslatableString("action", "Every .starscore in your projects folder, with what's left to audit in each")
             ),
    UiAction("starscore-compare-parts",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Compare parts…"),
             TranslatableString("action", "See which bars differ between parts on the same instrument")
             ),
    UiAction("starscore-convert-double-time",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Convert from double time…"),
             TranslatableString("action", "Rewrite a song written in double time in standard time: every note half as long, two bars become one, the tempo halved")
             ),
    UiAction("starscore-standardize",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Standardize this song…"),
             TranslatableString("action", "Give an older song the standard sections, arrangements and part scores")
             ),
    UiAction("starscore-toggle-minmaj",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Minor-major and diminished-major symbols in this part on/off"),
             TranslatableString("action", "Switch the minor-major and diminished-major seventh diamonds for the part being viewed")
             ),
    UiAction("starscore-toggle-panel",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "StarScore panel"),
             TranslatableString("action", "Show/hide the StarScore panel above the score"),
             Checkable::Yes
             ),
    UiAction("starscore-autosave-archive",
             mu::context::UiCtxAny,
             mu::context::CTX_ANY,
             TranslatableString("action", "Autosave archive…"),
             TranslatableString("action", "Open an earlier saved version of a song")
             ),
    UiAction("starscore-progress-colors",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Show progress colors"),
             TranslatableString("action", "Show or hide the finished / needs review / unfinished colors on measures. "
                                          "Hiding them keeps them. They never appear in exports."),
             Checkable::Yes
             ),
    UiAction("starscore-progress-finished",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Mark finished (green)"),
             TranslatableString("action", "Color the selected measures green: finished")
             ),
    UiAction("starscore-progress-review",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Mark needs review (orange)"),
             TranslatableString("action", "Color the selected measures orange: needs review")
             ),
    UiAction("starscore-progress-unfinished",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Mark unfinished (red)"),
             TranslatableString("action", "Color the selected measures red: unfinished")
             ),
    UiAction("starscore-progress-clear",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Clear progress color"),
             TranslatableString("action", "Take the progress color off the selected measures")
             ),
    UiAction("starscore-annotation-add",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Add player annotation…"),
             TranslatableString("action", "Add a band member's own note above the selected note or rest, on this sheet only. "
                                          "Exports print the sheet without it, plus a copy with that player's notes.")
             ),
    UiAction("starscore-annotation-mark",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Mark as player annotation…"),
             TranslatableString("action", "Make the selected markings a band member's own notes on this sheet")
             ),
    UiAction("starscore-annotation-unmark",
             mu::context::UiCtxProjectOpened,
             mu::context::CTX_ANY,
             TranslatableString("action", "Unmark player annotation"),
             TranslatableString("action", "Make the selected annotations ordinary markings of the sheet again")
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
    if (starScoreService()) {
        starScoreService()->panelVisibleChanged().onNotify(this, [this]() {
            m_actionCheckedChanged.send({ "starscore-toggle-panel" });
        });
        starScoreService()->progressColorsShownChanged().onNotify(this, [this]() {
            m_actionCheckedChanged.send({ "starscore-progress-colors" });
        });
    }
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

bool ProjectUiActions::actionChecked(const UiAction& act) const
{
    if (act.code == "starscore-toggle-panel") {
        return starScoreService() ? starScoreService()->isPanelVisible() : true;
    }
    if (act.code == "starscore-progress-colors") {
        return starScoreService() && starScoreService()->progressColorsShown();
    }
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
