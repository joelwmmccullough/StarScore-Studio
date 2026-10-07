/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Progress colors (Joel, 7 Oct 2026): a measure on a staff can be marked finished (green), needs review (orange) or
 * unfinished (red). The marks are kept in the song's score meta tag "starscoreProgressColors" and drawn only by the
 * score view, on top of the music; they are never part of the engraved score, so no PDF, print or other export shows
 * them. Hiding them (View › Progress colors) keeps the marks; showing them again brings them back.
 *
 * Stored text: entries "<measure EID>|<part id>|<staff in part>:<code>" joined by ';'. The code is 'g' (finished),
 * 'o' (needs review) or 'r' (unfinished). A measure is found by its EID, saved in the file, so the marks stay with
 * their measures when measures are inserted or deleted before them.
 */
#pragma once

#include <map>
#include <string>

namespace mu::engraving::starscore {
inline constexpr const char16_t PROGRESS_TAG[] = u"starscoreProgressColors";

//! Whether the score view draws the colors (set by StarScore from its setting; app-wide)
inline bool& progressColorsShown()
{
    static bool shown = false;
    return shown;
}

using ProgressMap = std::map<std::string, char>;

inline std::string progressKey(const std::string& measureEid, const std::string& partId, size_t staffInPart)
{
    return measureEid + "|" + partId + "|" + std::to_string(staffInPart);
}

inline ProgressMap parseProgress(const std::string& text)
{
    ProgressMap map;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(';', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        const size_t colon = text.rfind(':', end);
        if (colon != std::string::npos && colon > pos && colon + 1 < end) {
            map[text.substr(pos, colon - pos)] = text[colon + 1];
        }
        pos = end + 1;
    }
    return map;
}

inline std::string writeProgress(const ProgressMap& map)
{
    std::string text;
    for (const auto& [key, code] : map) {
        if (!text.empty()) {
            text += ';';
        }
        text += key;
        text += ':';
        text += code;
    }
    return text;
}
}
