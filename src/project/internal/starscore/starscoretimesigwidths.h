// Made by fonts/starscoretimesig/mkfont.py: each StarScore TimeSig font's digit 0-9 and small plus widths
// (1 em = 1000 = a time signature's height). Don't edit by hand.
#pragma once

struct StarScoreTimeSigWidths {
    const char16_t* musicFont;
    int widths[11];
};

static const StarScoreTimeSigWidths STARSCORE_TIMESIG_WIDTHS[] = {
    { u"Leland", { 404, 351, 392, 379, 457, 377, 402, 381, 408, 402, 254 } },
    { u"Bravura", { 470, 334, 446, 421, 470, 403, 434, 441, 436, 434, 254 } },
    { u"Petaluma", { 533, 303, 680, 560, 653, 603, 585, 612, 524, 514, 418 } },
    { u"StarScore Deco", { 375, 195, 285, 275, 320, 268, 335, 280, 355, 335, 265 } },
    { u"MuseJazz", { 317, 255, 313, 322, 370, 312, 318, 311, 339, 324, 274 } },
    { u"Gonville", { 424, 284, 376, 337, 384, 337, 390, 363, 400, 390, 263 } },
    { u"Emmentaler", { 366, 317, 366, 332, 399, 336, 339, 336, 366, 339, 250 } },
    { u"Finale Maestro", { 431, 306, 430, 414, 437, 382, 417, 448, 415, 417, 296 } },
    { u"Finale Broadway", { 391, 325, 390, 376, 415, 343, 331, 330, 293, 376, 254 } },
};
