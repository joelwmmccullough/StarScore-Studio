// Made by fonts/starscoretimesig/mkfont.py: each StarScore TimeSig font's digit 0-9 and small plus widths
// (1 em = 1000 = a time signature's height). Don't edit by hand.
#pragma once

struct StarScoreTimeSigWidths {
    const char16_t* musicFont;
    int widths[11];
};

static const StarScoreTimeSigWidths STARSCORE_TIMESIG_WIDTHS[] = {
    { u"Leland", { 397, 345, 385, 372, 449, 370, 395, 374, 401, 395, 254 } },
    { u"Bravura", { 454, 322, 431, 406, 454, 389, 419, 426, 421, 419, 245 } },
    { u"Petaluma", { 272, 154, 347, 285, 333, 307, 298, 312, 267, 262, 213 } },
    { u"StarScore Deco", { 374, 195, 285, 275, 319, 268, 334, 280, 354, 334, 265 } },
    { u"MuseJazz", { 271, 218, 268, 276, 317, 267, 272, 266, 290, 277, 235 } },
    { u"Gonville", { 436, 292, 387, 347, 395, 347, 401, 373, 412, 401, 271 } },
    { u"Emmentaler", { 362, 313, 362, 328, 394, 332, 335, 332, 362, 335, 247 } },
    { u"Finale Maestro", { 424, 301, 423, 407, 430, 376, 410, 441, 408, 410, 291 } },
    { u"Finale Broadway", { 331, 275, 331, 319, 352, 291, 281, 280, 248, 319, 254 } },
};
