# StarScore Studio changelog

StarScore Studio is built on MuseScore Studio. Newest version first. Builds before 1.0.0 count as 0.x versions.

Version numbers: the first number changes when files stop being compatible, the second for new features, the third for fixes and small changes.

## 1.4.0

- Each part score's tab shows a colored dot for its status (the same colors as the section and arrangement buttons). A part score holding several instruments shows the least-finished one. No dot means none of its parts is tagged yet.
- Right-click a part score's tab › Part status to set it straight from the part (it tags every instrument in that part score). Setting it from the section's menu still works.
- New sections start with their status on “Auto”.
- Rhythm section on “Auto”: drums, percussion and keys parts with no tag read the lead sheet, so they don't hold the section back. When the tagged parts (for example guitar and bass) are all Finished, the section shows **Finished — drums, percussion and keys use the lead sheet**, with a teal dot, and those players' sheets start unticked in the export. Tag one of those parts to count it like any other.

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
