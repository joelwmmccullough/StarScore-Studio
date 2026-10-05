# StarScore Studio changelog

StarScore Studio is built on MuseScore Studio. Newest version first. Builds before 1.0.0 count as 0.x versions.

Version numbers: the first number changes when files stop being compatible, the second for new features, the third for fixes and small changes.

## 1.18.7

- **Four Flexible Scores.** Each Flexible section now exports four Scores:
  - "Score": concert pitch; treble clef, the bottom horn in bass clef.
  - "Score (Bb)": written for B♭ horns; treble clef, the bottom horn in treble clef an octave down.
  - "Score (Eb)": written for E♭ horns; same clefs as the B♭ Score.
  - "Score (Bass Clef)": concert pitch; bass clef an octave up, the bottom horn in bass clef.
- **New clefs for writing Flexible parts.** The chairs are now written in the clefs that keep most of their range on the staff: Horn 1 in treble clef, the 3-Horn's Horn 2 in soprano clef (middle C on the bottom line), and the bottom chair (2-Horn Horn 2, 3-Horn Horn 3) in alto clef. Songs already made switch once when opened (only a chair still in its old treble or bass clef changes; Undo puts it back). The exported sheets don't change.
- **Show the Flexible chairs as Standard horns.** Right-click a Flexible section › Show the chairs › "As B♭ Trumpet, Tenor Sax" (2-Horn) or "As B♭ Trumpet, Alto Sax, Tenor Sax" (3-Horn) shows the chairs with those horns' transpositions and clefs (turn Concert Pitch off to see the written notes). The chairs keep their Flexible ranges. "Flexible clefs" switches back. The exported sheets are the same either way.
- **Songbooks is back, hidden by default.** The password on "Joel's Secrets" is gone and the page is called Songbooks again. It only shows on Home when it's turned on in Preferences › General › Songbooks › "Show the Songbooks page on Home". It's off by default, so people you give StarScore to don't see it.
- **"Installing" window during updates (Mac).** While an update replaces the app, a window says StarScore is installing and not to open it until it opens by itself. This starts with the next update after this one (the window comes from the version doing the installing).
- The first clef of a staff now follows a clef change everywhere: an exported sheet made from a chair written in another clef used to keep the chair's clef on its first system.

## 1.18.6

- **Two Scores per horn section: concert and transposing.** Each Standard section (and Big Band, Orchestra, Marching Band) now also exports "Score (Transposing)", with every horn at written pitch. The Flexible sections keep one Score.
- **Scores carry the arrangement top right**, like the parts: "3-Horn Arrangement", "Flexible 3-Horn Arrangement", "Orchestra Arrangement"…
- **Score systems from the parts.** When a section's Score in the song hasn't been formatted by hand, the exported Score takes the system breaks of the part with the most system breaks (page breaks and system locks aren't counted or copied), and puts the same bars on each system as that part's page. A Score whose breaks are all the same as another part score's (Top Hat's 3-Horn Score had the Lead Sheet's breaks, with only the intro on page 1) counts as not formatted.
- **Repeated sections on one page.** On Scores, a section between repeat signs that runs onto the next page gets a page break before it, when the whole section then fits on one page and the page before stays at least half full. With the Trumpet part's systems, Bumper Cars' bars 34–50 take six systems of three staves, one more than a page holds, so that section still runs over two pages (it now starts at the foot of page 2 instead of leaving bars 49–50 alone on the next page).
- **Measure numbers on Scores sit higher**, clear of the brackets (Bumper Cars' bar 6).
- **Trombone sheets in tenor clef too.** Every Flexible sheet for Trombone is exported twice: "Trombone" (bass clef) and "Trombone (Tenor Clef)".
- **Making a Flexible section from a Standard one copies everything** from the Standard part scores: breaks and system locks as before, and now also where texts, dynamics and chord symbols sit, which ones are hidden, and the bar widths.

## 1.18.5

- **Horn section Scores in the house order, with brackets.** Each exported Score lists and brackets its staves as follows (whatever order the instruments are in the song):
  - Flexible: Horn 1, Horn 2 (, Horn 3), all bracketed.
  - 2- to 4-Horn Standard: all bracketed. The doubler takes its place: Flute and Piccolo at the top, Clarinet and Soprano Sax after the trumpet, Bass Clarinet after the tenor (and the trombone). Two tenors are Tenor Sax 1 and 2.
  - 5-Horn: trumpets bracketed, saxes and clarinet bracketed, trombone alone. With Flute or Bass Clarinet: all bracketed. With Piccolo: piccolo and flute bracketed, trumpets bracketed, tenor and trombone alone.
  - 6- and 7-Horn: trumpets bracketed, saxes and clarinet bracketed, trombone alone, the 7th horn alone. With Flute: flute alone. With Bass Clarinet: bass clarinet alone in 6-Horn, bracketed with the 7th horn in 7-Horn. With Piccolo: piccolo and flute bracketed.
  - Piccolo: the Score also shows its Flute version (the flute is what's usually played), and the piccolo's staff is a small staff. In 2- to 4-Horn, piccolo and flute also get a curly brace, left of the bracket.
  - Big Band, Orchestra and Marching Band: section by section (saxes, trumpets, trombones, rhythm; woodwinds, brass, percussion, strings; woodwinds, brass, front ensemble, battery), each bracketed except the big band's rhythm section; a piano keeps its brace.
- **Flexible Scores name the chairs H1, H2, H3** on every system after the first (the chairs used to show their instrument's short name, "Tpt. 1", "Tbn. 1"). New Flexible sections get these short names in the song too.
- **Save before exporting.** Export to Sheets and Demos now asks "Exporting can occasionally cause StarScore to crash. Would you like to save first?" Yes saves and exports only once the save has worked; No exports without saving; Cancel Export goes back to the list.
- **Flexible folder colours count every chair.** A Flexible folder whose chair had no status tag (Top Hat's Horn 3) came out Green: sheets with an "empty" status were skipped there, a rule meant for the old optional flute staff, and an untagged chair counts as empty. Flexible folders now colour like any other.
- **Add arrangement only lists arrangements the song doesn't have yet.**
- **A Tenor Sax doubler is Tenor Sax 2.** New Standard sections with the doubler on Tenor Sax put the doubler after the section's own tenor, so it is numbered 2.
- **7-Horn with a Bass Clarinet doubler:** only the 7th chair gets the low-horn versions and goes in the "Bass Horns (Horn #7)" folder. Before, the Bass Clarinet doubler could be taken for the 7th horn, and the 7th horn's own Bass Clarinet version wasn't made.

## 1.18.4

- **The Flexible Flute sheet stays in a flute's range.** On export, the Horn 1 Flute sheet is printed as the chair is when every note is between C4 and C7; if even one note is outside, the whole sheet is printed an octave up. Bet's Horn 1 goes down to E3 (87 notes below a flute's low C), so its Flute sheet is now an octave up. A Flute sheet made into its own part ("Edit one sheet by hand") starts the same way.
- **Add arrangement lists each Flexible after its Standard:** 2-Horn Standard, 2-Horn Flexible, 3-Horn Standard, 3-Horn Flexible, then 4- to 7-Horn Standard.
- **Adding a section the song has no arrangement for** (a 4-Horn Section with no 4-Horn Standard arrangement) asks "Did you mean to create a new arrangement?" with a button to add that arrangement instead. "Add the section" (or closing the question) adds the section as before.

## 1.18.3

- **One home page.** The Dashboard now shows your recent scores on the left and the Starsign dashboard on the right (drag the line between them to resize). The separate Scores page is gone, and so are "My online scores" and the "Score manager (online)" button. The dashboard's buttons wrap under its text when its half is narrow, and Next up / By priority stack when there isn't room side by side.
- **Joel's Secrets.** The Songbooks page is now called Joel's Secrets and asks for a password. The first time it's opened you choose the password; after that it's asked for once each time StarScore starts (the lock button top right closes it again). The password is kept as a hash in this computer's settings. It stops the page being opened by accident or by someone else at this computer; the songbook files themselves aren't encrypted.
- **New songs start at version 0.0.0.** The first export of a new song suggests Major version, making it 1.0.0; tick Minor version (0.1.0) or Patch (0.0.1) instead for a work-in-progress export.
- **Jost ships with StarScore.** Jost (free licence) is built into the app for italic texts, and it stands in for Futura on computers that don't have Futura (Windows, Linux). Macs keep using their own Futura. TT Modernoir is not bundled: its trial licence allows internal testing and evaluation only and doesn't allow passing the font on.

## 1.18.2

- **Horn 1's Flute sheet is made from Horn 1.** The 3-Horn Flexible "Horn 1 - Flute" sheet is now made from the Horn 1 part score like Horn 1's other sheets, with its formatting. Until now it came from a hidden "Horn 1 (Flute)" staff with a part score of its own that had to be formatted by hand (Bet's Flute sheet had the old title style and the wide pickup bar). New 3-Horn Flexible sections no longer get that staff; in existing songs it stays in the file but isn't used.
- **Edit one Flexible sheet by hand.** Right-click a Flexible section › Edit one sheet by hand › pick a sheet ("Horn 1 - Alto Sax"…). The sheet becomes a part of its own in the section: the chair's music on that instrument, hidden in the score, with its own part score, which opens with the chair part score's formatting. From then on the export prints that part as it is instead of making the sheet from the chair, and it no longer follows changes to the chair. Picking it again opens its part score. Delete the part to go back to the sheet made from the chair.
- **Long arrangement labels make room for the title.** When the label at the top right of a sheet runs into the song title (Balkan Wedding's "Flexible 2-Horn Arrangement"), it is shortened to "Flexible 2H Arrangement"; if it still runs into the title, it goes on two lines, "Flexible / 2H Arrangement", then "Flexible 2H / Arrangement".
- Version sheets made from a fresh part (a chair with no part score of its own) and the 7-Horn and Piccolo stand-in versions now also take the source part score's bar widths and hidden texts.

## 1.18.1

- **Flexible version sheets keep all of the chair's formatting.** Each "Horn N - <instrument>" sheet is now printed from the chair's own part score ("3H Flexible: Horn 1"), re-pitched and re-clefed for the instrument, instead of from a fresh part that only got the chair's line breaks and text positions. Everything set in the chair's part score carries over: hidden texts stay hidden (Bet's hidden "test" text printed on every version), bar widths (Bet's narrowed pickup bar), text positions, spacers and the chair's own style. A chair with no part score of its own still gets the fresh part as before.
- **Mute markings only for brass.** On Flexible version sheets for saxes, clarinets, flutes and strings, texts that are only a mute or open marking ("mute", "(mute)", "open", "(open)", "cup mute", "con sord.", "senza sord."…) are left out. Trumpet and trombone sheets keep them. Longer texts that happen to contain "open" ("Open solos") stay.
- **Version suggestion fixed.** Exporting unchanged music suggested a major version every time. The song kept export records of sheets in folders that no longer exist (Bet's old "3H Tpt Flu Ten"), and each export counted them as removed sheets. A sheet now counts as removed only while its file is still in the song folder, and each export forgets records of sheets that are no longer planned. Sheets the dialog starts unticked (Percussion, and any you untick) are left out of the suggestion, so an unexported Percussion sheet no longer counts as "new" every time.

## 1.18.0

- **Flexible version sheets were never transposed.** Every "Horn N - <instrument>" sheet of a Flexible arrangement (and every transposed lead sheet in a Songbook) came out in the chair's own concert pitch and treble clef, whatever the file name said: Alto Sax, Trumpet, Viola and Bari Sax sheets identical to the Violin's. The export asked MuseScore to replace the chair's instrument at bar 1, but MuseScore keeps a part's main instrument at a tick of its own, found nothing to replace there and did nothing. Fixed: the sheets are in the instrument's key and clef, chord symbols transposed with them (a bari sax reads E7♯9 for a concert G7♯9). This has been wrong since Flexible sections got per-chair sheets (1.9.0), so every Flexible folder and Songbook exported since then needs re-exporting; the old sheets go to Version History as usual. Balkan Wedding and Bumper Cars weren't affected: their Flexible sections are the older kind with one real staff per key.
- **The export suggests the version number.** Export to Sheets and Demos now ticks one of the three numbers for you and says why, from what changed in the music since each sheet's last export (the bar-by-bar signatures kept since 1.15). The three boxes are now called Major version, Minor version and Patch. The rules: **Major** when the song itself changed — it is longer or shorter, the rehearsal marks changed, key or time signatures changed, the lead sheet changed in a quarter or more of its bars, or a sheet that was exported before is not in this export. **Minor** when notes, chord symbols, dynamics or slurs changed in some bars of any sheet, or a sheet is new (a new arrangement, a Flute version). **Patch** when nothing in the music changed — layout, text and style fixes. A song exported before change tracking existed gets no tick on its first export (there is nothing to compare with yet). Untick or tick another box to override; the export never changes the number on its own.
- **Flexible version sheets keep the chair's text positions.** Texts moved by hand in the chair's part score ("mute", "(open)", the tempo mark) are placed the same way on every version made from it, as they are for the bass horn versions.

## 1.17.1

- **A piccolo is a piccolo.** A piccolo part was labelled "Flute" on export (Bet's flute sheet was really the piccolo). It is now exported as "CODE - Piccolo.pdf", its horn folder is named with "Pic" ("3H Tpt Pic Ten"), and the sheet's name and the arrangement's score say Piccolo. On the first export after this build, the sheets in the folder's old name ("3H Tpt Flu Ten") move to Version History and the empty old folder is removed, as happened when the Flexible folders were renamed. The organizer and the Players guide know the piccolo too.
- **A Flute version of the piccolo.** Marking a Piccolo part Finished offers to create a Flute part from it, the way the 7-Horn bass trombone offers its Bari Sax / Bass Sax / Bassoon… versions: the flute gets the same written notes (so it sounds an octave below the piccolo), the piccolo part's page and system breaks and text positions, status Needs review, and it sits below the piccolo in the same section, shown and hidden with it and left out of the folder's horn count ("3H", not "4H"). The section menu's "Make the Piccolo's Flute version…" does the same on request. This works in any Standard, 1-Horn or custom horn section; Flexible sections, Big Band, Orchestra and Marching Band are left alone. A song's colour doesn't wait for the flute (as it doesn't for the bass horn versions); the folder's own colour does, like any sheet in it.
- **Add arrangement asks the same questions as New.** Adding a 3- to 7-Horn Standard arrangement from the StarScore bar's Add arrangement menu now asks what the woodwind doubler plays ("<name> on Alto Sax (recommended)" for 3- to 5-Horn, Soprano Sax to start for 6- and 7-Horn, the name from the band roster) and, for 7-Horn, the preferred 7th horn, when the horn section has to be made. An arrangement whose horn section already exists, 2-Horn and Flexible arrangements, Big Band, Orchestra and Marching Band are added at once as before.

## 1.17.0

- **Chord charts in Export to Sheets and Demos.** When the Lead Sheet is Finished (and the song isn't in Works In Progress), the export also writes a "Chord Charts" folder with "CODE - Chord Chart.pdf" (one tall phone-width page, sections mirroring the rehearsal marks, repeats and endings written out where they help) and "CODE - iReal Pro.html" (the iReal Pro import link, split into parts when the song is too long for one chart). Both end with a small note giving the export's version number. Replaced charts go to Version History like sheets; unchanged ones are left alone. Made from the lead sheet's chords; a text's "Visible on chord chart" box (1.15.7) decides whether it appears.
- **New score: Starsign or not.** File › New first asks "Starsign Score" or "Non-Starsign Score"; Non-Starsign opens MuseScore's own new-score wizard. The Starsign page lists the starting arrangements in order: 2-Horn Standard, 2-Horn Flexible, 3-Horn Standard (recommended, the default), 3-Horn Flexible, 4-, 5-, 6- and 7-Horn Standard. Big Band, Marching Band and Orchestra are no longer starting points (the templates still exist).
- **Which horn the doubler plays.** For 3- to 7-Horn Standard a second dropdown picks the saxophone chair's instrument: Soprano Sax, Alto Sax, Tenor Sax, Clarinet, Bass Clarinet, Piccolo or Flute. The player's name comes from the band roster on this computer ("<name> on Alto Sax"), never from the app itself. Alto Sax is recommended for 3- to 5-Horn; 6- and 7-Horn start on Soprano Sax.
- **Preferred 7th horn.** 7-Horn Standard gets a third dropdown: Bass Trombone (recommended) or Bari Sax, Bass Sax, Bassoon, Bass Clarinet, Contrabass Clarinet, Contrabassoon or Tuba. The chosen instrument is the section's main 7th horn: its sheet is the one in the arrangement, and marking it Finished offers the other seven versions (Bass Trombone included) made from its music, just as the Bass Trombone's versions were. The export puts the main low horn and its versions in the "Bass Horns (Horn #7)" folder whichever instrument it is.
- The dialog remembers the last arrangement, doubler and 7th-horn choices.
- **Windows build.** Every "[dmg]" build now also produces a Windows installer (StarScore-Studio-Windows.msi, unsigned, so SmartScreen will warn once) after the Mac dmg is published, and the Linux AppImage is added to the same release. The Windows installer installs into its own "StarScore Studio" folder and never touches an installed MuseScore Studio.

## 1.16.0

A maintenance release from a full review of the StarScore code: four reviews, about 110 findings, the ones below fixed. Exported sheets are unchanged (every Balkan Wedding and Bumper Cars PDF was compared page by page with the previous build).

- **Updates from inside the app.** Help › Check for update (and a check once per launch) asks GitHub for a newer StarScore build. If there is one, a dialog shows that version's changelog; Update downloads the dmg to your Downloads folder as "StarScore Studio X.Y.Z.dmg", quits, replaces the app in Applications, ejects the dmg and relaunches. An existing file of that name is left alone (the download goes to "… (2).dmg"). If anything goes wrong the dmg is opened in Finder instead, and `~/Library/Logs/StarScore Studio/update.log` says what happened. Builds published before this one have no version in their release name, so the first update the app can find is the one after 1.16.0.
- **Export to Sheets and Demos is faster**: Balkan Wedding's 74 sheets in 7 minutes instead of 10 on the Linux test machine. The song is now loaded from disk once per export instead of once per Flexible sheet (16 times), part books are laid out once per sheet instead of two to six times, the PDF is compared, fingerprinted and written from memory instead of a temporary file read back three times, and the export plan is made once instead of three times.
- **Export safety.** If a sheet can't be archived to Version History, the old file is now left in place and the export reports it (before, it was deleted and the new file written anyway). New files are written beside the old one and swapped in, so a crash can't leave a half-written sheet. The organizer's codes.json is written the same safe way. A reference PDF after an unchanged sheet no longer fails with "the reference PDF is missing". A song that isn't registered yet is refused with a clear message instead of writing into the band folder's root. A Flexible version sheet whose packed PDF stream happened to end in a line-end byte was judged "changed" and re-archived on every export; fixed.
- **Organizer safety.** A data file that exists but can't be read (a half-synced or corrupt recordings.json, changelog.json, maintenance log, codes.json or sheet cache) stops the run with a message instead of being treated as empty and overwritten. Edits made while a run is going (the Recordings window, the roster, "Add song") are merged into the files at the end instead of being overwritten. Logic/GarageBand/Pages bundles dropped in 6 Inbox or a song folder are filed as one item instead of being taken apart file by file, and no longer inflate the file counts. Two shows on one day get distinct ids. A new recording's id can no longer reuse a deleted one. The Maintenance Report shows the last twelve months (the log keeps everything).
- **Several on-open fixes never ran.** A second listener for "project changed" was silently dropped by the event system, so the reference-PDF panel's restore timers and the composer-credit placement on the reopened part score never fired. Fixed; these now run.
- **Snappier panel.** The StarScore panel used to re-read the song's section data a dozen times for every edit (every note typed); it is now parsed once per change and the panel updates once per event-loop turn. The Arrangements/Sections/Solos chips are rebuilt only when their own list changed. The style settings file is read once instead of on every call.
- **Ordinary MuseScore files render as MuseScore does.** StarScore's chord-symbol tweaks (simpler root spelling, the bass-slash pull-back, no kerning) now apply only to scores using StarScore's chord file; a plain .mscz gets MuseScore's own spacing and kerning back. Part names keep their ♭ in plain-text uses (MusicXML export, the mixer). The "Visible on chord chart" box shows only for .starscore files.
- **Smaller fixes:** the new-score template fallback picked 2-Horn Standard where 3-Horn Standard was meant; a part-score status change on a solo transcription marked the wrong file unsaved; closing the last score could crash if no score was open; selecting a text no longer assigns it an id just by being looked at; the recent-files takeover from MuseScore Studio runs once rather than on every launch; a toolbar that grew while condensed now gets its room back; timers that could fire after the service was gone are tied to it; several regular expressions are built once instead of per file.

## 1.15.10

- **The window can be much narrower** (down to 500 px wide, about half a laptop screen), for working next to another window such as YouTube. As the window gets narrower, the score page makes room in steps:
  - The top toolbars condense: first the extensions and the Parts/Mixer buttons go, then the playback controls keep only their buttons (no time, bar/beat or tempo), and last the page tabs keep only Home and the current page.
  - The note input buttons wrap onto a second and third line instead of being cut off.
  - Side panels (Palettes, Layout, Properties, To-do and anything else docked at the side) close once the score would have less than 480 px, right-hand side first. They reopen, in the same order, when the window is wide enough again.
  - The Arrangements and Sections chips wrap, so the bar's buttons stay in reach, and the status bar moves the workspace and concert pitch controls into its "···" menu.
  Widening the window brings everything back.

## 1.15.9

- **Re-exports no longer archive sheets that didn't change.** A sheet whose pages look exactly the same is left alone, as intended. Since 1.15.6 nearly every re-export archived and rewrote every sheet: two exports of the same pages store their fonts in a different order, and a position can differ by a ten-thousandth of a unit, so the files never matched byte for byte. The pages are now compared by what they draw. (Balkan Wedding's Version History gained 120 copies on 3 Oct this way; they are kept.)
- **Balkan Wedding's Bass Sax credit 2.5 pt lower than the other horn sheets.** When the title frame had to grow to fit the credit, the credit (aligned to the bottom of the frame) moved down with it. It is now placed again once the frame has grown.

## 1.15.8

- **Score PDFs: composer credit back in the title frame.** Every Score.pdf printed the credit about 17 mm too low, beside the first system (Bumper Cars, Bet, Another One, Top Hat, Always There). Apply Styles measured the main score in continuous view, where the title frame's texts aren't where they print, and moved its credit down; Score PDFs are printed from the main score. The credit is now measured in page view, and each Score PDF puts it back at its usual place (last line on the subtitle's baseline) when it is printed. The next Apply Styles also fixes the main score itself.
- **Lead Sheet and rhythm sheets: credit placed at export too.** Only horn sheets had their composer credit placed when exported; the Lead Sheet and rhythm sheets kept whatever position they had unless you had opened that part score in 1.15.7, so a long credit (Balkan Wedding) still printed above the instrument name. Every part score is now placed at export.
- **The part score a song reopens on** gets its composer credit placed too (until now only part scores you switched to were checked).
- **Song folder colour:** an empty chair in a Flexible folder (the 3-Horn Flexible's optional flute) no longer holds the song's colour back either, as it already didn't for the folder's own colour.
- **The old "NH Any Horns" folder is removed once it is empty** (its sheets are archived to Version History, as in 1.15.7). A folder with anything else left in it stays as it is.

## 1.15.7

- **Composer credit placed the same way on every sheet.** Its last line sits on the subtitle's baseline, but its first line is never higher than the instrument name on the left (a long credit moves down), and it moves below the arrangement label if there is one. Balkan Wedding's Bass Sax (credit over the label) and Bass Clarinet (too low) come out like the other horn sheets, and the Lead Sheet and rhythm sheets no longer have the credit above the instrument name. Fixed when a part score is opened or exported. An arrangement label moved to the right by hand is recognised too.
- **"NH Any Horns" folders are now "NH Flexible"** ("2H Flexible", "3H Flexible"). The next export writes the new folder and archives the sheets in the old one to Version History.
- **Flexible folder colours:** a chair's sheet with nothing in it (the 3-Horn Flexible's optional flute) no longer keeps the folder gray.
- **"Visible on chord chart"** for staff and system text: a tick box in the text's properties, ticked by default. Untick it to leave that text off the song's chord charts (made from the lead sheet), such as a note to yourself about notation. Saved with the song; undoable.

## 1.15.6

- **Stray bars after an export.** Exporting printed each part score with its sheet title added and then undid it; undoing an edit after the part was laid out could leave bars behind in the score (Balkan Wedding gained an empty bar and a broken multimeasure-rest bar before bar 32 during the 4.0.1 export). The export no longer changes and undoes anything around the printing: a part score that doesn't show its sheet title yet gets it for good.
- **Part scores keep the score's barlines.** The barline check (on opening and exporting) now also compares every part score with the score: a part that lost a double barline the score has (before H1's repeat in Balkan Wedding's Bass Sax, Bari Sax and Contrabassoon) gets it back.
- **New Bass Trombone versions copy where the texts sit** from the Bass Trombone part: staff and system text ("Final soloist continues playing", "End solo"), the tempo mark, rehearsal marks and expressions.
- **Tempo marks in the right font.** A tempo mark could keep an old font for the part after its note ("= 100" in Petaluma Script on Balkan Wedding's Drums). Fonts written into tempo marks are removed when a song opens, so they use the tempo style's font.
- **Title frames of a fixed height** in every score and part score when a song opens (a frame sized to its contents came out too tall: Balkan Wedding's Drums).
- **Finished sheets keep their layout.** Marking a part, a section or a part score Finished locks every system that isn't locked yet and adds a page break after the last system of each page (except the last page, and where there's a break already).
- **New sections open their part scores**, each with its sheet title (Flexible chairs show "Horn 1" and "Flexible 2-Horn Arrangement").
- **Flexible ("Any Horns") sheets are written again.** Each chair's sheet for every instrument ("Horn 1 - Trumpet in Bb", "Horn 2 - Trombone", …) wasn't found once the chair had been renamed, so only Score.pdf reached the "NH Any Horns" folder (Balkan Wedding's 2- and 3-Horn Flexible). The sheets are now found by their part, not their name; the same for transposed songbook sheets.
- **Exported scores name their staves without the section** ("Trumpet 1", not "7H: Trumpet 1").
- **Starsign 2.6 style:** Staff Text and Text Lines in Futura (were Jost). Applied the next time part styles are applied.
- **Less space between the arrangement label and the composer credit:** about 6 pt of white space, as on Balkan Wedding's Bari Sax, Bass Sax and Bass Clarinet sheets (most sheets had 13.5 pt). The credit could only ever move down, so a credit moved too far stayed there; now it is placed from its usual position each time (last line on the subtitle's line, then just below whatever it would run into), so it can move back up. On horn sheets this includes credits placed in the part score itself, so every horn sheet gets the same gap; on other sheets, a credit placed by hand keeps its place unless something runs into it. Fixed when a part score is opened or exported. Exporting also checks the label and credit of part scores that already have their sheet title (a new Bass Trombone version printed with the credit over its label).

## 1.15.5

- **The arrangement label stays on the instrument name's line.** At export, the label was lined up with the instrument name directly, outside the edits that are undone after printing; now that part scores keep their label, that could leave it moved up out of the title frame (Balkan Wedding's Bari Sax). Lining it up is now undone with the rest after printing, and opening a part score puts its label back on the instrument name's line before moving the composer credit clear of it.
- **Barlines match on every staff.** MuseScore keeps a barline per staff, so an instrument added later starts with plain barlines where the others have double or final ones (the new Bass Trombone versions lost the double barline before each repeat). When a song opens and when it's exported, any bar where some staves have a double, final or other special barline and the rest a plain one gets it on every staff, in the score and every part. Bars with repeat signs, or with two different special barlines, are left alone. The check is quick: about a hundred bars across a few dozen staves.

## 1.15.4

- **Faster first opening in 1.15.3+.** The first time a song opens, its horn parts are renamed ("7H: Bari Sax") and its horn part scores get their sheet titles. That was done one part at a time, each laying out the score and every part score again (Balkan Wedding took minutes); it's now one step. Once the file is saved, opening it does nothing extra.
- **Faster "Create parts".** New parts (Bass Trombone versions, new sections) get their names and short names in one step instead of two layouts of every score per part. Adding instruments to a song with dozens of part scores still takes a moment, mostly MuseScore laying out the scores and remaking the arrangement's score.
- **Composer credit and arrangement label, in StarScore.** Part scores whose credit still ran into "N-Horn Arrangement" (most of Balkan Wedding's horn parts) are fixed when you open the part score: the credit moves down there and then. The check now goes by height only (both sit at the right edge), and a credit placed by hand moves by its own position.
- **Stand-in parts are yours to format after they're made.** New Bass Trombone versions copy its layout and barlines when they're made; after that StarScore leaves their formatting alone. Versions made before 1.15.3 get their barlines matched once (1.15.3 did it on every opening).
- **Bass Clarinet version** is made again: it used an instrument MuseScore only keeps internally, so it was skipped. It's now the Bass Clarinet in B♭.
- **Faster export:** after printing each part score, StarScore no longer lays it out again straight away (it's laid out when you next look at it).
- **More room under the arrangement label.** A composer credit moved down clear of "N-Horn Arrangement" now leaves about half a line between them (it was half a staff space).
- **PDFs say which StarScore made them.** Every exported PDF's Creator (shown as its author and creator in Finder's info) is now "StarScore Studio Version: 1.15.4 (MuseScore Studio Version: 4.7.5)". A re-export whose only difference is a newer StarScore still counts as unchanged and leaves the file alone.
- **To-do list PDF.** Every Export to Sheets and Demos also makes "CODE - To-Do.pdf" (e.g. "BALK - To-Do.pdf") next to the song's .starscore in Projects and Sheets: the To-do tab as a printed page. Page 1 has how many steps and parts are finished, what's next, and the steps in priority order with what each still needs; page 2 lists every part by section with its status, and the Big Band, Orchestra and Marching Band full scores. It's made fresh each export (it doesn't go in Sheets and Demos, since it's about the work on the song).
- **7-Horn folder colour** needs every bass horn sheet: Bass Trombone, Bari Sax, Bass Sax, Bassoon, Bass Clarinet, Contrabass Clarinet, Contrabassoon and Tuba (in the Bass Horns subfolder). Until all are there, the 7-Horn folder is gray. Takes effect on each song's next export.
- **Bass Horns (Horn #7) subfolder colours** of its own, each counting only sheets exported as Finished: Red with none of these, Orange with the Bass Trombone, Yellow with the Bass Trombone, Bari Sax and Bass Sax, Green with all eight.

## 1.15.3

- **Horn part names** in StarScore are "section: horn": "7H: Trumpet 1", "7H: Bari Sax", "3H: Tenor Sax", "1H: Alto Sax", "3H Flexible: Horn 1". The part's name in the score and its part score's name follow. Existing files are renamed when opened (save afterwards). The exported sheets keep their own names ("BALK - Trumpet 1.pdf", printed "Trumpet 1 in B♭"). Part-style rules that name a part work with or without the section.
- **Part scores show their exported title.** A horn part score now shows, in StarScore, the instrument name its sheet prints top left ("Trumpet 1 in B♭", "Baritone Saxophone") and the arrangement top right ("7-Horn Arrangement"), as the exported sheet will. Done when a file opens, when part styles are applied and when parts are made.
- **The composer credit clears the arrangement label.** A long credit (Balkan Wedding) moved clear of the title and subtitle but ran into "N-Horn Arrangement" at the top right. It now moves below that too, in StarScore and on the exported sheet.
- **The Bass Trombone's other versions:**
  - Also Bass Clarinet, Contrabass Clarinet, Contrabassoon and Tuba, besides Bari Sax, Bass Sax and Bassoon.
  - Only the versions the 7-Horn section doesn't have are made; a deleted one is made again.
  - Right-click the 7-Horn section › "Make the Bass Trombone's other versions…" makes them any time (not only when the Bass Trombone is marked Finished).
  - New ones get the Bass Trombone part score's page breaks, system breaks and system locks, its double barlines (before repeats, etc.), and their part scores open.
  - Stand-in versions made before keep in step: their barlines are matched to the Bass Trombone's when the file opens.
- **Title frame size.** Applying the house style now makes the title frame a fixed 15 sp. Part scores StarScore made had frames sized to their contents, which ignored the height and came out far too tall.
- **Blank arrangement scores fixed.** An exported score showed only the title when its instruments were hidden staff by staff (the eye on each staff in the Instruments panel): Balkan Wedding's 2- to 5-Horn scores. The export now shows those staves, and showing a section or arrangement on the StarScore bar does too.
- **Export to Sheets and Demos:**
  - The percussion sheet is "Percussion" (it was named after the instrument, "Congas"). The organizer renames old "Congas", "Bongos", "Timbales" and "Cajon" sheets to Percussion, and the export moves them to Version History once a Percussion sheet is written.
  - The 7-Horn arrangement's Bass Trombone and its versions go in a subfolder, "Bass Horns (Horn #7)". The sheets in the old place move to Version History; changelogs carry on from them.
  - Percussion sheets no longer count toward the 1 Rhythm folder's colour (an old Congas sheet turned it gray). The 7-Horn folder's colour also counts the sheets in its Bass Horns subfolder, and the subfolder gets its own colour.
- **Copy part formatting:** ticking a part no longer scrolls the list back to the top.

## 1.15.2

- **Keys sheet staff settings.** Every keyboard part's bass-clef staff is set to "Hide empty staves: Always hide" (in the score, the arrangement scores and the Keys part score), and the Keys part score's style has "Automatically hide all empty staves" on and "Don't hide empty staves in first system" off. This is applied when you apply part styles and on every export to Sheets and Demos, so existing songs get it on their next export.

## 1.15.1

- **Bari Sax, Bass Sax and Bassoon versions are part of the 7-Horn section.** "Create Bari Sax, Bass Sax, Bassoon versions" now makes them visible below the Bass Trombone (hidden only while the 7-Horn section is off), adds them to the section so they show and hide with it, puts them in the 7-Horn arrangement's score, and marks them Needs review. Before, they were made hidden and were easy to miss. Files where they were made hidden (Balkan Wedding) are fixed once when opened; save the file afterwards.
- **The Deco button is gone from the StarScore bar.** Switch StarScore Deco on and off in the bar's "…" menu (StarScore Deco font, ticked when on).

## 1.15.0

- **To-do panel** (a tab beside Palettes, Layout and Properties; View › To-do). The open song's work in priority order: 3-Horn Section, bass + guitar, lead sheet, drums, 2-Horn Section, 2-Horn Flexible, 3-Horn Flexible, keys, 4-Horn Section, the 1-Horn sheets, 5-, 6- and 7-Horn Sections, percussion, Big Band, Marching Band. Each step shows how far along it is (its least finished part, with the status colours of the StarScore bar), what still needs work, or "Not in the score yet". "Next up" at the top is the first step that isn't Finished. Drums, keys or percussion that read the lead sheet count as the lead sheet. The 1-Horn step needs Trumpet, Alto Sax, Tenor Sax and Trombone; the 7-Horn step needs the Bari Sax, Bass Sax and Bassoon versions of the Bass Trombone. The list updates as you change statuses.
- **Full scores of Big Band, Orchestra and Marching Band have their own status.** Set it on the arrangement's score tab as usual; it no longer tags every part in it. The arrangement counts as finished (StarScore bar, Dashboard, songbook charts, To-do panel, folder colours) only when every part *and* its full score are marked Finished. Other arrangements' scores work as before.

## 1.14.4

- **Long composer credits don't run into the title.** After the composer text is lined up with the subtitle, StarScore checks whether it now reaches up into the title or the subtitle (Balkan Wedding lists many composers). If so it moves down until it clears them by half a staff space, and the title frame grows if the credit would otherwise hang into the music.

## 1.14.3

- **Title-frame text follows the style.** Applying the house style now also clears a size, font or font style set on the title, subtitle, composer or lyricist text itself, including size and font tags written into the text. These came in with imported files and overrode the style: Bet's and Two's subtitles stayed at 15 pt instead of the style's 21 pt. Other files with such settings: Big Milk (title 28 pt), G.I. Jorge (title font), Another One (subtitle and composer font), Playground (composer font).
- **Composer lines up with the subtitle.** Applying the house style now puts the last line of the composer text on the subtitle's baseline (it hung a little below it). StarScore measures both after layout and adjusts the composer position in each score's and part book's style; a position set on the subtitle or composer text itself is cleared.

## 1.14.2

- **Bar numbers match on every sheet.** In Bet and Two the pickup bar is excluded from the measure count in the main score and the horn parts, but the lead sheet and rhythm part books still counted it, so their bar numbers ran one ahead of the horn sheets (MuseScore keeps "exclude from measure count" and "add to measure number" separately in each part book). Export to Sheets and Demos now copies both settings from the main score to every part book before printing, and says so in its summary. The fix is one undo step; save the file to keep it. Songbooks and Export arrangements as MuseScore files do the same on their copies.

## 1.14.1

- **Keyboard sheets are always "Keys".** Export to Sheets and Demos names the piano, electric piano, organ, clavinet or synth sheet `CODE - Keys.pdf` (two keyboard parts: `Keys (Piano)`, `Keys (Organ)` …). Once the Keys sheet is in 1 Rhythm, an older keyboard sheet there under its old name (`Elec Piano`, `Organ`, `Clavinet`, `Piano`) moves to Version History. The folder organization files loose keyboard sheets from 6 Inbox as Keys too.

## 1.14.0

- **Folder colours from what's finished and exported.** Every Export to Sheets and Demos now records each sheet's status as it was exported (in `6 Inbox/.organizer/sheets/CODE.json`), and the folder organization colours folders from that. A sheet counts only while the file in the folder is still the one exported.
  - **Song folders.** Each colour also needs everything the colours below it need.
    - Purple: the Big Band and Marching Band charts.
    - Blue: every sheet the songbooks and the 4- to 7-Horn charts need.
    - Green: every horn sheet of 1-Horn, 2-Horn Flexible, 2-Horn Standard, 3-Horn Flexible, 3-Horn Standard and 4-Horn Standard.
    - Yellow: the 3-Horn Section, drums, guitar, bass, keys and the lead sheet.
    - Orange: the 3-Horn Section, guitar, bass and the lead sheet.
    - Red: the Yellow sheets, each exported at least as Sketch.
    - Gray: less than that.
    - All "Finished" except Red. Drums or keys that read the lead sheet count as the lead sheet; percussion is left out.
  - A song not exported since this version has no colour. Export it again to colour it; sheets that come out the same aren't rewritten.
  - **Sheet folders** (1 Lead Sheet, 1 Rhythm, each horn folder, Big Band …): Gray when a sheet the folder must have is missing, otherwise Green / Yellow / Orange / Red by the least finished file in it (Finished / Needs review / In progress / Sketch). A file with no status (not exported from StarScore, or changed since) makes it Gray. 1H must have Trumpet, Alto Sax, Tenor Sax and Trombone; a 7-horn folder must have the Bari Sax, Bass Sax and Bassoon versions of the Bass Trombone; 1 Rhythm must have guitar and bass, plus drums and keys when there's no lead sheet.
  - Each tune folder in Projects and Sheets gets its song's colour.
- **Bassoon version of the 7-Horn Bass Trombone.** Marking a 7-Horn Bass Trombone part Finished now offers whichever of the Baritone Sax, Bass Sax and Bassoon versions it doesn't have yet (songs that already have the two saxes are offered just the bassoon). The bassoon sheet is exported into the 7-horn folder like the saxes.

## 1.13.1

- **Organizing after an export now runs.** In 1.13.0 the organizer window closed the moment it opened after an export (a window opened from the export window closed with it), so nothing was organized. It now opens once the export window has closed.
- **Re-exports that change nothing leave the files alone.** A sheet that comes out the same as the file already in Sheets and Demos isn't rewritten or moved to Version History (the PDFs are compared without their export date and random document id). The export summary says how many were left as they were, and when nothing changed, the song's Update Notes keep their date. The song's own pages are still rebuilt, so its Horn Part Guides come from the latest score. The same applies to Export arrangements as MuseScore files.
- **Folder shortcuts are never followed.** In Projects and Sheets, the empty-folder sweep went into "10 Sweater Weather", a shortcut to another band's shared Drive folder. It walked that whole folder (very slowly) and could have moved its empty folders. Shortcuts are now skipped everywhere the organizer files or sweeps, and Stop now also stops the sweep.
- **Projects and Sheets folders are coloured too**: each tune folder gets the same Finder colour as its song folder in Sheets and Demos (matched by code). Tunes with no song folder keep whatever colour they have.
- **Folder colours** follow the old script's scoring again: the lead sheet, the rhythm section and the 3-horn chart each count 1 when done and ½ when started; 3 is green, 2 or more blue, anything above 0 yellow. (1.13.0 left out the halves, so a song with one part done and two started showed yellow instead of blue.)
- **A first run that was cut short picks up where it left off**: the old toolkit's sheet measurements are read from Deprecated/Retired, so the sheets aren't all measured again, and the new RULES.md stays where it is.

## 1.13.0

- **The folder organizer is now part of StarScore.** It replaces the Python scripts and the two daily tasks (off since 21 Aug). It runs after every Export to Sheets and Demos (the new "Run organization process" box, ticked by default), and from File › Run folder organization or the Dashboard. It:
  - files 6 Inbox and loose sheets, gives numbered song folders without a code their code, and keeps Update Notes dated to each song's last change;
  - finds blank and short sheets, reading only new or changed files;
  - writes changelog entries for the players whose parts changed. After an export they name the bars and rehearsal letters, e.g. "3-Horn Arrangement: your Alto Sax part changed in bars 33–48 (letter C)";
  - rebuilds the PDFs that are out of date: What's Here, Recordings, changelogs, Horn Part Guides (now worked out from the score), the Band Guide, the Progress tracker and the Maintenance Report, plus All Recordings and the Projects Maintenance Report. The PDFs are made with the Mac's own WebKit and look like before;
  - in Projects and Sheets, files 9 Inbox and loose top-level files (older MuseScore material into the tune's MuseScore Files/), moves empty folders to "Z Empty Folders (safe to delete)", and keeps codes_proj.json in step with codes.json;
  - colours the song folders in Finder.
  Nothing is ever deleted. The first run moves the old Python toolkit to Deprecated.
- **New shows**: each run checks setlist.fm. A show is offered from the day after its date. Paste the YouTube link, or skip it, or mark it "not filmed". The songs and their times come from the video's description; you confirm any names StarScore can't place.
- **Play counts** come from setlist.fm: this year plus last year.
- **Recordings window** (File › Recordings…, or "…" › Recordings…): this song's live takes, albums and sessions. Rate live takes with 1–5 stars; best-rated come first on the Recordings pages. A copy is kept in the .starscore.
- **Band roster** (File › Band roster…, or the Dashboard): who reads which parts, horn chairs for Any Horns charts, current or former. A new player on an instrument starts with the former player's changelog history.
- **Rebuild everything** (File menu, Dashboard): every generated PDF in both folders.

## 1.12.14

- **Reference PDF panel**: closed when a file opens, unless it was open when you last closed that file. It's now one setting per file (before, each part score had its own, so closing the panel in a part could leave it open for the main score next time). The notation page no longer brings the panel back from its saved layout; StarScore decides. Each score and part still shows the PDF last chosen for it.

## 1.12.13

- **Export to Sheets and Demos** (and songbook sheets): the arrangement name at the top right is leveled with the instrument name when the sheet is printed. After layout, StarScore measures where both texts landed and moves the arrangement name to the instrument name's height. (The 1.11.3 and 1.12.2 fixes didn't change its position; it was still one staff height too low.)

## 1.12.12

- **StarScore Deco 0.12**: the s in sf, sfz, sfp and the other s dynamics is turned 6° instead of 4°.

## 1.12.11

- **StarScore Deco 0.11**: the bass clef is Joel's pick from every version so far (the 0.7 design with one stencil break across the top) with a smaller ball. In sf, sfz, sfp and the other s dynamics the s sits closer to the f and is turned 4° so its bottom clears the f's tail.

## 1.12.10

- **StarScore Deco 0.10**: the s in dynamics and the turn follow Joel's own sketch: the 0.3 shape with a smooth concave curve inside each end instead of the small counters.

## 1.12.9

- **StarScore Deco 0.9**: the s in dynamics and the turn no longer have jagged spots where the end cuts meet the inside curves (the curves were too tight for the stroke and folded; the s is now a little wider so they can't). The segno is frozen exactly as Joel approved it. The bass clef's ball flows into the stroke without a neck, and its dots are true circles.

## 1.12.8

- **StarScore Deco 0.8**: the bass clef's head is a clean round ball with a slim neck (Joel: "make the nose less ugly"), keeping the one stencil break across the top. The s in dynamics, the turn and the segno lose the tiny spike and notch at the inner corners of their ends.

## 1.12.7

- **StarScore Deco 0.7**, from Joel's review of 0.6:
  - The s (dynamics, turn, segno) is back to the 0.3 design Joel chose, cleaned up so its inner curves and ends have no small steps.
  - Bass clef: one stencil break only, across the top.
  - The diminished-major diamond is removed; the minor-major diamond stays. The minus is a little heavier and sits at the middle of the chord's digits.
  - The 4's stem is nudged further toward the middle.

## 1.12.6

- **StarScore Deco 0.6**, from Joel's review of 0.4:
  - New glyphs for Joel's own chord marks: the minor-major seventh (a diamond with a full bar through it) and the diminished-major seventh (the plain diamond), at U+F4C1 and U+F4C0.
  - The triangle, plus and minus chord marks are as heavy as the diminished circle.
  - Bass clef: the head grows out of the stroke again and the dots are solid; the stencil breaks stay.
  - The 4 is narrower with its stem nearer the middle, so it lines up with the other figures in time signatures.
  - The ends of the s (dynamics, turn, segno) and the opening of the e in Ped. are cut square to the stroke.
  - Trill, arpeggio and glissando wiggles end in a clean square end instead of half a corner.

## 1.12.5

- **StarScore Deco 0.5**: stencil clefs refined. The treble clef's stem is slightly heavier; the bass clef's stroke starts with a clean cut just above the head, with an even gap; the alto clef's arms meet the bowls without a step.

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
