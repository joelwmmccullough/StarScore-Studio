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

#ifndef MU_ENGRAVING_SYSTEMTEXT_H
#define MU_ENGRAVING_SYSTEMTEXT_H

#include "stafftextbase.h"

namespace mu::engraving {
//---------------------------------------------------------
//   SystemText
//---------------------------------------------------------

class SystemText : public StaffTextBase
{
    OBJECT_ALLOCATOR(engraving, SystemText)
    DECLARE_CLASSOF(ElementType::SYSTEM_TEXT)

public:
    SystemText(Segment* parent, TextStyleType = TextStyleType::SYSTEM, ElementType type = ElementType::SYSTEM_TEXT);

    bool isEditAllowed(EditData&) const override;

    SystemText* clone() const override { return new SystemText(*this); }
    Segment* segment() const { return (Segment*)explicitParent(); }

    bool canBeExcludedFromOtherParts() const override { return true; }

    // StarScore (Joel, 20 Oct 2026): a system text breaks a multimeasure rest only when it's ticked to (Pid::BREAK_MMR,
    // off by default). One that doesn't is drawn on the rest where its bar falls: mmRestPlace() is that place, from 0
    // (the rest's first bar) towards 1, on the copy shown on the rest; -1 elsewhere.
    bool breaksMMRest() const { return m_breaksMMRest; }
    void setBreaksMMRest(bool v) { m_breaksMMRest = v; }
    double mmRestPlace() const { return m_mmRestPlace; }
    void setMMRestPlace(double v) { m_mmRestPlace = v; }

    PropertyValue getProperty(Pid id) const override;
    bool setProperty(Pid id, const PropertyValue& v) override;

protected:
    PropertyValue propertyDefault(Pid id) const override;

private:
    bool m_breaksMMRest = false;
    double m_mmRestPlace = -1.0;
};
} // namespace mu::engraving
#endif
