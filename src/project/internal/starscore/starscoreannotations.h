/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Player annotations: finding them in a part score, and taking them out of it for a moment to print the sheet
 * without them (see engraving/dom/starscoreannotations.h for what they are and how they are kept).
 */
#pragma once

#include <set>
#include <string>
#include <vector>

#include <QString>

namespace mu::engraving {
class EngravingItem;
class MasterScore;
class Score;
class Segment;
}

namespace mu::project::starscore {
struct AnnotationItem {
    engraving::EngravingItem* item = nullptr;
    QString player;
};

//! The player annotations in a score, in score order: none in the main score
std::vector<AnnotationItem> annotationItems(engraving::Score* score);

//! The keys (annotationKey) of every item in the song's part scores that could be an annotation (the marks of
//! items no longer there are dropped by comparing with these)
std::set<std::string> liveAnnotationKeys(engraving::MasterScore* master);

//! Takes annotations out of their score and puts them back, without the undo stack, as undoing and redoing their
//! adding would: the sheet is laid out and printed as if they had never been there. Whatever is still out is put back
//! when this goes. Nothing may be edited in the score meanwhile.
class AnnotationDetacher
{
public:
    explicit AnnotationDetacher(engraving::Score* score);
    ~AnnotationDetacher();
    AnnotationDetacher(const AnnotationDetacher&) = delete;
    AnnotationDetacher& operator=(const AnnotationDetacher&) = delete;

    void detach(const std::vector<engraving::EngravingItem*>& items);
    void attach(const std::vector<engraving::EngravingItem*>& items);
    void attachAll();

private:
    struct Detached {
        engraving::EngravingItem* item = nullptr;
        engraving::Segment* segment = nullptr;   // a segment taken out with it (a breath's own, left empty)
    };
    engraving::Score* m_score = nullptr;
    std::vector<Detached> m_detached;   // in the order taken out
};
}
