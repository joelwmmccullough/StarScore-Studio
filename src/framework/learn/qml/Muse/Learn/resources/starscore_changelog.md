# StarScore Studio changelog

StarScore Studio is built on MuseScore Studio. Newest version first. Builds before 1.0.0 count as 0.x versions.

Version numbers: the first number changes when files stop being compatible, the second for new features, the third for fixes and small changes.

## 1.12.4

- **StarScore Deco 0.4**, from Joel's review of 0.3:
  - Treble, bass and alto clefs are now the stencil design: narrow breaks where strokes meet or cross. In the treble clef the stem stops short on both sides of every stroke it crosses; the thickest strokes are lighter. The bass clef's head is a separate disc, with breaks across the top, the heavy side and the tail, and split dots. The alto clef has smaller ball ends, a split thick bar, and arms that stop short of the thin bar.
  - The 4's stem moved left (and the 1 and 3 were adjusted) so figures stacked in a time signature line up.
  - A subtler 8, a wider and more open s (dynamics, turn, segno), and a cleaner e in Ped.

## 1.12.3

- **StarScore Deco 0.3**, redrawn from Joel's glyph-by-glyph review of 0.2:
  - Figures (time signatures, tuplets, 8va/15ma/22ma, octave clef numbers) are each one even-width stroke, so no part is heavier than another and curves run into straight lines without steps. New 1 flag, a real 8 with a pinched waist, wider 3 and 5.
  - Double sharp is an X of four tapering arms (no more square blocks). The flat's bowl meets the stem horizontally, giving a full round bottom. The natural has no small steps where its parts meet.
  - Marcato is symmetric. The very long fermata has equal bars and gaps, and the very short fermata's dot no longer touches the chevrons.
  - Treble and bass clefs redrawn with thick-and-thin contrast, a ball at the start of the treble clef's curl, a pointed top loop, and a bass clef tail that thins to a point. The alto clef's bowls match.
  - The f, r and s in every dynamic are rebuilt with clean hooks and a straight-spined s. The trill's t no longer runs into the r, and the turn and segno use the new s.

## 1.12.2

- **Export to Sheets and Demos** (and songbook sheets): the arrangement name at the top right is now actually level with the instrument name. A title-frame text created by StarScore defaulted to “below” placement, which pushed it down by a staff height. It now takes the instrument name's placement.

## 1.12.1

- **StarScore Deco 0.2**: every glyph redrawn to one style sheet measured from TT Modernoir Bold, so the whole font now hangs together and matches the text next to it.
  - Modernoir is nearly monoline (its O walls are only 10% heavier than its top and bottom); the font was drawn with 2.5–4:1 contrast. All strokes now use one pen with a stress of 0.82.
  - Bowls are true circles and ellipses with small oval counters, as in Modernoir, instead of the squared shapes.
  - Figures have Modernoir's own widths (0 wide, 1, 3 and 5 narrow) and its constructions: the 2 and 6 flow into straight tangent diagonals, the 3 and 5 have open round bowls, the 1, 4 and 7 end in sharp points.
  - Dynamics letters have Modernoir's proportions (narrow s, f and r; wide m and p), semicircular arches, quarter-circle hooks, tiny slit counters, and “ff” shares one continuous crossbar. Also used in tr, Ped., 8va/15ma letters.
  - Clefs, accidentals, rests, flags, articulations, fermatas, ornaments, chord-symbol marks and repeat signs are on the same stroke weight (0.30 sp for symbols, 0.36 for figures, 0.25 for letters).

## 1.12.0

- **Deco button** in the StarScore panel: one press switches the main score and every part book to the StarScore Deco music font (music symbols, music text and dynamics). Pressing it again brings back exactly the fonts each one had before. The fonts to restore are saved in the file, so switching off works after saving and reopening too. The button is highlighted while Deco is on. Only the fonts change: line thicknesses and other style settings stay as they are.

## 1.11.3

- **Export to Sheets and Demos**: the arrangement name at the top right of a horn sheet is on the same line as the instrument name at the top left, with the same vertical alignment, offset and size. The part book's instrument name is often moved by hand, and the arrangement name didn't follow it.

## 1.11.2

- **StarScore Deco**: ball ends now grow out of their strokes. The line swells smoothly to the full width of the ball instead of a ball being stuck on the end: the treble clef's tail, the bass and alto clef knobs, the eighth-rest family and the breath-mark comma.

## 1.11.1

- **StarScore Deco**: cleaned up the joins where shapes meet, so there are no more small steps or spurs.
  - Dynamics letters now have square shoulders where bowls meet stems, the s is redrawn, and an f is spaced from its crossbar so it no longer runs into the letter before it.
  - Also fixed: the flat, the alto clef's chevron, the quarter and eighth rests, the figures 1–7, the common and cut time C, the short fermatas, the trill and mordent ends, the segno, the ø slash and the pedal and 8va letters. The brace is bolder, and the 16th and 32nd notes in tempo marks have full-length stems.

## 1.11.0

- **StarScore Deco** (new music font, first draft): an art deco music font drawn to go with TT Modernoir. Choose it in Format › Style › Score › Musical symbols font; the musical text font is “StarScore Deco Text”. The Starsign style still uses Petaluma until you switch.
  - It covers noteheads (including slash, x and diamond heads), clefs, accidentals, rests, flags, time signatures, dynamics, articulations, fermatas, repeat signs, segno and coda, ornaments, tremolos, pedal marks, octave signs, chord-symbol marks and metronome notes. Anything it doesn't have yet is drawn from Bravura.
  - The font and the scripts that draw it are in fonts/starscoredeco.

## 1.10.0

- **1-Horn arrangement** (new template): a 1-Horn Section with a melody sheet for each songbook horn (Trumpet, Alto Sax, Tenor Sax, Trombone), with the lead sheet and rhythm section. You write these sheets by hand; the lead sheet stays the full overview of the song.
  - Songbooks: a horn book's Solo sheet is now that instrument's 1-Horn sheet, as written, instead of the lead sheet transposed. It's ready when the 1-Horn Section is Finished. Piano, guitar and bass books still use the lead sheet.
  - Dashboard: a 1-Horn column. In the priority order, originals' 1-Horn comes after covers' 3-Horn Flexible and before originals' 4-Horn; covers' 1-Horn comes after covers' 4-Horn.
  - Export to Sheets and Demos puts the 1-Horn sheets in a “1H” folder, one sheet per horn, titled “1-Horn Arrangement”. The mixer gives them the 4-Horn Section's levels.
- **Older songs**: marking a sheet Finished marks it as no longer needing an audit, and marking a section Finished does the same for all its sheets. An arrangement whose sheets are all marked that way counts as audited. If the music of such a sheet changes afterwards, the arrangement shows as changed since the audit. Sheets already marked Finished before this version count too. Setting a sheet back to something other than Finished takes the mark off.
- **Export to Sheets and Demos**: the arrangement name (“3-Horn Arrangement”) now sits at the top right of each horn sheet. It was showing under the instrument name on the left.
- **Changelog** has its own page on Home, below Learn, instead of a tab inside Learn.
- **Dashboard**: Next up lists the existing arrangements to audit or finish first. After those, it goes on to recommend which missing arrangements to write next, in the same priority order as the audit list.
- **Audit**: the “Differs from the reference section” check is gone, and so is the “Compare with” choice. The library audit reads every song again once to update the counts.
- **Audit and section switches**: an instrument shown because you clicked an audit item is now temporary. It's hidden again when you click another item, it doesn't make its section look switched on, and it isn't remembered as one of the section's shown instruments. Switching a section on or off after using the audit now shows or hides the whole section as expected.
- **Mixer**: StarScore now remembers, in the file, which tracks it muted because their instrument was hidden. Showing an instrument again unmutes it after reopening the file or coming back from a part book. Tracks muted on purpose (the lead sheet piano, chord-symbol tracks, congas) stay muted.

## 1.9.0

- **Songbooks** (Home › Songbooks): builds printable books and charts from the parts that are done.
  - **Album songbooks** for Tenor Sax, Alto Sax, B♭ Trumpet, Trombone, Piano, Guitar and Bass. Each song gets an opening page (with your notes, if you've written them) and then:
    - horn books: Solo (the lead sheet in the instrument's key), and the instrument's Duo and Trio lines from the 2- and 3-Horn Flexible arrangements, transposed and in its clef, titled “Duo · Bottom line” and so on;
    - rhythm books: the lead sheet and the instrument's part from the rhythm section.
    The book starts with a cover, “How to use this book” and contents, with headings in TT Modernoir and page numbers throughout.
  - **Song charts**: 4-, 5-, 6- and 7-Horn, Big Band, Marching Band and Orchestra. A folder with the arrangement's score and every part, each titled with the instrument and the arrangement.
  - Only finished parts go in: Finished for Top Hat, Bet, Another One and new songs; audited for songs converted from older files. The page shows, song by song, which sheets are ready and why the others aren't. Songs that aren't ready are left out of a book and listed in it as not in this edition yet.
  - Album tracklists can be edited (Edit tracklist); Ichiban and Feed Your Kids Bugs are filled in.
  - In horn books, bars the lead sheet writes in bass clef (a bass riff) are rests marked “(bass)”, and the whole Solo sheet is in treble clef.
  - Songbooks are saved in “Songbooks” next to your songs (Projects and Sheets) unless you pick another folder. A book or chart written again moves the old one to Songbooks/Deprecated/<date>.

## 1.8.0

- **Dashboard** (Home › Dashboard, now the first page of Home): every song in 1 Starsign Originals and 2 Starsign Covers, with a square for each arrangement (3-Horn, 2-Horn, 2- and 3-Horn Flexible, 4- to 7-Horn) colored by how far along it is.
  - **Next up**: the list of what to do next, in priority order: originals' 3-Horn, 2-Horn, 2-Horn Flexible and 3-Horn Flexible arrangements, then the same for covers, then originals' 4- to 7-Horn, then covers' 4- to 7-Horn. Within each, the songs played most in 2026 (setlist.fm) come first, and the one closest to done breaks a tie. Click a task to open the song (with the Audit panel when it needs auditing).
  - **Done** means Finished for Top Hat, Bet, Another One and any new song, and audited for the songs converted from older files.
  - **By priority**: a progress bar for each of the 16 groups, with the next song in each.
  - **Audit preview** for each song: things to look at, likely errors and listen-throughs approved.
  - “Audit these in order” goes through the songs that need auditing one at a time, in priority order, with the Audit panel.
  - Songs are read in the background and remembered; only songs that changed are read again (Refresh).
- “N-Horn Any” is now called **N-Horn Flexible** everywhere in StarScore. Existing sections and arrangements are renamed when a file is opened. (The Sheets and Demos folders are still called “NH Any Horns”.)
- A new 2- or 3-Horn Flexible section filled from the Standard section also takes each part's formatting: Horn 1's part score gets the Trumpet part's style and system and page breaks, Horn 2 the Alto Sax's (Tenor Sax's in 2-Horn), and so on.
- New sections get the default mixer settings once their instruments' sounds are loaded. Before, they were applied too early and didn't take.
- The Reference PDF panel no longer opens with songs that don't use it (it could be reopened by the page restoring its panels after the song opened).
- Chord symbols: C♭, F♭, E♯, B♯ and double sharps or flats in a chord's root or bass are written as the easier note (B, E, F, C, …), including in transposed parts.
- **Copy part formatting** (Format menu and the StarScore “…” menu) replaces “Apply system formatting from another part” and “Apply this system formatting to other parts”: choose the part to copy from (the part you're viewing to start with) and tick the parts to copy into.
- A multimeasure rest no longer shows a double barline that belonged to bars it used to end on. A double barline written at the end of its last bar still shows.

## 1.7.0

- Exported horn sheets (Export to Sheets and Demos) have the horn's name at the top left (“Trumpet in B♭”) and the arrangement at the top right (“2-Horn Arrangement”). This is done only while printing; the part scores themselves don't change.
- “Any Horns” sections export one sheet for every instrument that can play each chair (Starsign Band Guide, page 3), each with its own name at the top left and “Flexible N-Horn Arrangement” at the top right. For example Horn 1 of a 2-horn arrangement becomes Soprano Saxophone, Clarinet in B♭, Trumpet in B♭, Alto Saxophone and Violin sheets, each transposed and in its clef. Files are named “CODE - Horn 1 - Trumpet in Bb.pdf”. Sheets with the older names (“Horn 1 in Bb”) are moved to Version History when you export.
- The 7-Horn section's Baritone and Bass Saxophone versions of the Bass Trombone don't count as extra players in the folder name (still “7H …”), and aren't in the section's score.
- New StarScore: a Subtitle field.

## 1.6.1

- Chord symbols: the diminished circle (C♯°7) is a smaller ring in the chord font's line weight, with its top level with the extension, instead of Bravura's large thin circle.

## 1.6.0

- **Saxophone versions of the 7-Horn Bass Trombone part**: when you mark the 7-Horn section's Bass Trombone part Finished, StarScore asks whether to create Baritone Saxophone and Bass Saxophone parts. “Create parts” adds both to the 7-Horn section with the Bass Trombone's music, transposed for each saxophone, and sets them to Needs review. They're hidden in the score and have their own part scores and sheets. Check them there: notes too low for a saxophone are colored as out of range. It asks once; after the parts exist it doesn't ask again.
- The audit treats them as versions of the Bass Trombone line: they're compared with it (a note moved up an octave shows as a minor issue), they're checked for range, and they aren't counted as extra lines or played in the listen-through. On “Auto”, the section's status counts them.
- New files: the lead sheet instrument's short name is “Lead” (its long name already was), and the other instruments get their own short names without MuseScore's automatic numbers. Before, the rhythm section's piano was numbered (“Pno. 2”) because the lead sheet is also a piano.
- The Reference PDF panel opens with a file only if it was open when that file was last closed. New files, and files that never had it open, start with it closed. A part score opened for the first time follows the main score.

## 1.5.1

- Audit library works with no score open, and is also in the File menu (under Open recent). Before, it was greyed out until a score was open.
- Audit all songs and the Audit library open each song in the same window, in place of the song that was open (asking to save it first, as closing does). Before, each song opened in a new window, without the Audit panel. If you cancel at the save prompt, you stay on the current song.
- Audit all songs: it now always opens the first song on the list. Before, if the open song was also on the list, it stayed on that song and nothing seemed to happen. When the first song is the one already open, it shows the Audit panel with the song strip.

## 1.5.0

- **Audit all songs** (Audit library › Audit all songs): opens the songs that still need work one at a time, in the library's order, with the Audit panel showing. A strip at the top of the Audit panel says which song you're on (“Song 3 of 42 · Bet”) and has Previous song, Next song (Finish on the last one) and Stop. Moving on asks to save as usual. The place in the list is kept after quitting, so you can pick up where you left off. If nothing has been checked yet, it goes through every song in the folder.
- The lead sheet uses the normal part staff size (7.5 mm) again, like other single-staff parts. Its bass staff, which only shows where it has music, made it count as a two-staff sheet and get the smaller keys size (6.5 mm) since 1.2.0. Only the keys sheet uses the smaller size. Existing lead sheets change when you use “Apply part styles now”.

## 1.4.5

- A ♭, ♮ or ♯ typed as a plain character in text (for example the part name “Trumpet in B♭”) is drawn with the music font's accidental, at the same size and position as an inserted accidental symbol. Before, text fonts without that character fell back to a thin, oversized flat with a gap in front of it.

## 1.4.4

- A rehearsal mark that would touch a chord symbol right after it slides a little to the left (up to 4 spaces) instead of jumping up above the chord, so it stays at its usual height.
- Part styles dialog: removed the outdated line naming the built-in default style.

## 1.4.3

- Selecting bars across a multimeasure rest no longer grabs far more than you picked (for example selecting bars 47–48 selected all of page 2). The rest inside a multimeasure rest kept an old, wrong length (hundreds of bars) after the bars it covers changed, and that length was saved in the file. It is now corrected whenever the score is laid out, so existing files are fixed as soon as they are opened, and saving them stores the right length. (A MuseScore bug.)
- Slash chords: after an extension (G♭△7/B♭) the slash sits closer, so the chord and its bass read as one unit. Chords without an extension (C/E) are unchanged.
- The installer disk is named “StarScore Studio 1.4.3” (the StarScore version) instead of “StarScore-Studio-4.7.5”.

## 1.4.2

- Mixer defaults: trumpets use MS Basic › Choose automatically instead of MS Basic › Trumpet, so “mute” text switches them to the muted sound. Songs set up earlier get this when you use “Apply part styles now”.

## 1.4.1

- Installer window: the “STAR” sticky note covers “muse” in the logo, and the instructions say “drag the StarScore Studio icon”.

## 1.4.0

- Each part score's tab shows a colored dot for its status (the same colors as the section and arrangement buttons). A part score holding several instruments shows the least-finished one. No dot means none of its parts is tagged yet.
- Right-click a part score's tab › Part status to set it straight from the part (it tags every instrument in that part score). Setting it from the section's menu still works.
- New sections start with their status on “Auto”.
- Rhythm section on “Auto”: drums, percussion and keys parts with no tag read the lead sheet, so they don't hold the section back. When the tagged parts (for example guitar and bass) and the Lead Sheet section are all Finished, the section shows **Finished — drums, percussion and keys use the lead sheet**, with a teal dot, and those players' sheets start unticked in the export. Until the lead sheet is finished, the section shows the lead sheet's status if that is lower. Tag one of those parts to count it like any other.

## 1.3.0

- **Part status**: every part (instrument) can have its own completion tag: Empty, Sketch, In progress, Needs review or Finished. Right-click a section › Part status › the part.
- **Auto section status**: a section's Status can be set to “Auto”. It then shows the least-finished status of its parts. A part with no tag counts as Empty; parts the section keeps hidden (like Congas) don't count.
- A double (or other) barline at the end of a bar followed by a start repeat is no longer lost when the score is saved. It used to disappear from any score or part where the two bars were on the same system at save time, so it went missing in parts where they're on different systems. Barlines already lost need to be added once more; after that they stay.
- Audit library: clicking a song no longer crashes the app, and the library audit checks only “1 Starsign Originals” and “2 Starsign Covers” (these were 1.2.1, which was not released on its own).
- Multimeasure rests: the H-bar's horizontal stroke is 0.70sp thick (was 1.00sp), in the Starsign style and on every sheet when part styles are applied.

## 1.2.0

Based on MuseScore Studio 4.7.5.

**Audit mode** (View › Audit, or “Audit this song” in the StarScore “…” menu)

- **Checks** tab: a list of things to look at in the parts. Click one to select its bars in the score.
  - “Any Horns” chairs written out in several keys: every version is compared with the others (notes, rhythm, ties, articulations, slurs, dynamics, and octave changes).
  - Each horn line is compared with its closest line in a reference horn section (the biggest standard horn section unless you pick another).
  - Horn lines that follow the lead sheet melody and break from it for one bar.
  - Bars with the wrong number of beats, key signatures that don't match the lead sheet, local time signatures, invisible notes, and notes set not to play.
  - Notes outside the instrument's range, voices that cross, one-bar unisons, and entrances after four or more bars' rest with no dynamic.
  - Mark an issue “Intentional” to stop seeing it. Show or hide minor issues and intentional ones.
  - Filter by arrangement, and mark an arrangement audited. An audited arrangement is flagged again if its music changes.
- **Listen** tab: each horn section at each rehearsal mark where it plays, biggest section first and the standard section before the “Any” section. Only that section is shown and heard while you listen (optionally with the rhythm section). Approve to move on to the next one; playback stops at the end of the rehearsal section. “Done” shows the instruments that were showing before.
- **Audit library** (Format menu, the StarScore “…” menu, or the Library button in the Audit panel): every .starscore in your projects folder, with what's left to look at, how many arrangements are audited and how many listens are approved. Songs with the most to look at come first; click one to open it with the Audit panel. Songs that haven't changed aren't read again.

**Other changes**

- The Reference PDF panel remembers, for the main score and each part score, which PDF was showing or that the panel was closed, across quitting and reopening. This memory now follows the file when it is moved or renamed.
- Bold and italic text in a variable font (for example rehearsal marks in “TT Modernoir VF Trial”) keeps its weight in exported PDFs: the installed static fonts (“TT Modernoir Trial”) are used for it.
- The lead sheet's bass staff is set to “Hide when empty: Always”, so it only appears in systems where it has music. This applies to new lead sheets, imported files, and “Apply part styles now”.
- The window title says “StarScore Studio” when no score is open.
- The title bar shows the StarScore document icon for .starscore files.
- This changelog (Home › Learn › StarScore changelog).

## 1.1.1

- Reference PDF panel: pinch zoom on the trackpad zooms around the point under the cursor.
- Rehearsal marks at the start of a system sit in the same place whether or not the bar starts with a repeat sign.
- StarScore “…” menu › Version number (x.y.z)… sets the score's version number directly.
- Chord symbols in StarScore Jost: parentheses around the whole chord use the font's own ( and ).

## 1.1.0

- Reference PDFs: a manager in the Reference PDF panel (⚙) to rename, reorder, delete, and mark each PDF with the instrument or part it's for. Renamed PDFs are exported under their new names.
- Each part score remembers which reference PDF it shows; a PDF marked with the part's instrument is picked automatically.
- Importing a reference PDF that's identical to one already in the score asks first.
- Reference PDF panel: pinch to zoom on the trackpad.

## 1.0.0

The first numbered release, based on MuseScore Studio 4.7.5. It includes everything built before it:

- **.starscore files**: sections (Lead Sheet, horn sections, “Any Horns” sections, Rhythm Section, …) and arrangements in one score, switched from the StarScore panel, with a status for each section. New StarScore, templates for band, big band, marching band and orchestra, and importing MuseScore files (sections and arrangements guessed, optional standardizing).
- **Export to Sheets and Demos**: every part and score as PDFs in the song's folder (replaced files go to Version History), adding new songs with their code, “Any Horns” chairs in every key and clef, reference PDFs, and exporting each arrangement as its own MuseScore file.
- **Reference PDFs** stored inside the .starscore, shown beside the score (with Invert for dark mode), exported as they are.
- **Solo transcriptions** stored inside the .starscore.
- **Styles**: Starsign 2.6 bundled as the default style, part-style rules, house settings, version number in the footer, mixer defaults, applying system formatting from one part to others.
- **Chord symbols**: StarScore Jost, triangles and minor-major / diminished-major symbols matched to the chord font, minor hyphen, raised Mc.
- **Checks**: Compare parts, Check voice order, Check instrument ranges, color notes by pitch.
- Additive time signatures; instruments hidden in the score are muted; StarScore icons and splash screen.
