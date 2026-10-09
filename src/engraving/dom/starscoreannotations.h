/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Player annotations (Joel, 9 Oct 2026): band members' own notes on their sheets ("breathe", a circled entrance, a
 * fingering), kept in the song so they survive sheet updates. An annotation is an ordinary item (staff text, a
 * dynamic, a fingering, a line…) that is only in one part score, marked as a player's. Export to Sheets and Demos
 * prints each sheet without annotations, and prints one more copy per player with only that player's annotations,
 * into the song's "Annotated Sheets" folder.
 *
 * The marks are kept in the song's score meta tag "starscoreAnnotations": entries "<item EID>|<item type>=<player>"
 * joined by ';'. The item is found by its EID, saved in the file with it; the type is there too because MuseScore
 * keeps a deleted item's EID registered under its address, which a new item can be given. The score view highlights
 * annotations on screen.
 */
#pragma once

#include <map>
#include <string>

#include <algorithm>

#include "engravingitem.h"
#include "measure.h"
#include "segment.h"
#include "spanner.h"

namespace mu::engraving::starscore {
inline constexpr const char16_t ANNOTATIONS_TAG[] = u"starscoreAnnotations";

//! annotationKey() -> player
using AnnotationMap = std::map<std::string, std::string>;

//! "<EID>|<type>", or nothing for an item without an EID yet
inline std::string annotationKey(const EngravingItem* e)
{
    const EID eid = e ? e->eid() : EID::invalid();
    if (!eid.isValid()) {
        return std::string();
    }
    return eid.toStdString() + "|" + e->typeName();
}

//! A player's name as kept in the tag: no ';' or '=', no line breaks, trimmed
inline std::string cleanPlayerName(const std::string& name)
{
    std::string out;
    for (char c : name) {
        if (c != ';' && c != '=' && c != '\n' && c != '\r' && c != '\t') {
            out += c;
        }
    }
    const size_t a = out.find_first_not_of(' ');
    const size_t b = out.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : out.substr(a, b - a + 1);
}

inline AnnotationMap parseAnnotations(const std::string& text)
{
    AnnotationMap map;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(';', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        const size_t eq = text.find('=', pos);
        if (eq != std::string::npos && eq > pos && eq + 1 < end) {
            map[text.substr(pos, eq - pos)] = text.substr(eq + 1, end - eq - 1);
        }
        pos = end + 1;
    }
    return map;
}

inline std::string writeAnnotations(const AnnotationMap& map)
{
    std::string text;
    for (const auto& [eid, player] : map) {
        if (!text.empty()) {
            text += ';';
        }
        text += eid;
        text += '=';
        text += player;
    }
    return text;
}

//! The item an annotation mark goes on: a line's segment counts as its line
inline const EngravingItem* annotationOwner(const EngravingItem* e)
{
    return e && e->isSpannerSegment() ? toSpannerSegment(e)->spanner() : e;
}

//! A text MuseScore copies into a multimeasure rest (a part score's rests are joined) stands for the one in the bar
//! underneath: the copy has no EID of its own
inline const EngravingItem* mmRestOriginal(const EngravingItem* e)
{
    const Measure* m = e ? e->findMeasure() : nullptr;
    if (!m || !m->isMMRest()) {
        return e;
    }
    for (const EngravingObject* l : e->linkList()) {
        if (l == e || !l->isEngravingItem() || const_cast<EngravingObject*>(l)->score() != const_cast<EngravingItem*>(e)->score()) {
            continue;
        }
        const EngravingItem* li = static_cast<const EngravingItem*>(l);
        const Measure* lm = li->findMeasure();
        const EngravingObject* parent = li->explicitParent();
        if (lm && !lm->isMMRest() && parent && parent->isSegment()) {
            const auto& list = static_cast<const Segment*>(parent)->annotations();
            if (std::find(list.begin(), list.end(), li) != list.end()) {
                return li;
            }
        }
    }
    return e;
}

//! The kinds of item that can be a player's annotation: markings, not the music itself
inline bool canBeAnnotation(const EngravingItem* e)
{
    if (!e) {
        return false;
    }
    switch (e->type()) {
    case ElementType::STAFF_TEXT:
    case ElementType::EXPRESSION:
    case ElementType::DYNAMIC:
    case ElementType::FINGERING:
    case ElementType::ARTICULATION:
    case ElementType::BREATH:
    case ElementType::SYMBOL:
    case ElementType::FERMATA:
    case ElementType::TEXTLINE:
    case ElementType::HAIRPIN:
        return true;
    default:
        return false;
    }
}
}
