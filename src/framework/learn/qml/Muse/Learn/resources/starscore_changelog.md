# StarScore Studio changelog

StarScore Studio is built on MuseScore Studio. Newest version first. Builds before 1.0.0 count as 0.x versions.

Version numbers: the first number changes when files stop being compatible, the second for new features, the third for fixes and small changes.

## 1.20.0

- **Practice, hands free.** "Hands free" (on the Practice page, beside each day, and in the runner) plays a whole day without stopping, so the laptop can sit on a stand behind the keyboards. Each part of each item opens, its play-along backing starts by itself, its timer runs, and the runner moves on when the time is up. The notation is fitted to the window with no scrolling ("Full screen" fills the screen). Pause, Skip, and Stop are at the top.
- **Breaks.** On the Practice page, set "Breaks: every N min, for M min" before making a routine or plan (0 for no breaks). Hands free stops for the break after about that much practice and starts again by itself; "End the break" starts sooner. In each break (and at the end of the day) it asks how the items since the last break went, one tap each (Started, Getting there, Solid, Mastered), so your progress still counts. "Skip ratings" marks them done and keeps how solid they were.
- **An item's parts each have their own timer.** An item with several exercises (the six scales of Okane's "Lydian lens") lists them as parts, each with its own minutes. The clock counts down the current part and moves to the next one by itself.
- **Play example and Play along.** Each part has "Play example" (its bars as written, at today's tempo, once) and "Play along" (a backing track of bass, drums, and a soft pad, without your notes, looped for the part's minutes). When an item has a tempo range, the play-along starts at today's tempo and steps up toward the goal pass by pass; the runner shows the pass and its tempo. iReal Pro is now a small button: there's no way for another app to start iReal or set its tempo.
- **Bigger or smaller text and notation in the runner.** "Text − 100% +" and "Score − 100% +" at the top, from 70% to 200% (text) and 60% to 200% (score). A bigger score reflows to fewer bars a line, so it still fits the width. Both are remembered.
- **Parts, Comping, and Solos.** The setlist has three areas now, with proper column headers. What 1.19 called harmony (scales per chord, voicings, and the like) is under Solos or Comping, wherever it's used.
- **Saved setlists.** The dropdown at the top of the setlist holds every setlist you've made, each with its own show date, needs, minutes, breaks, and plans. "New" makes one (a copy of the current one, with a name and a date), "Rename" and "Delete" do what they say. The Oct 31 setlist and its plans are the first one.
- **Want more? Add minutes.** Under an open day ("Want more? Add 30 minutes"), and in the runner when a day is done (15, 30, 45, or 60 min), adds new items to the end of the day. They're chosen from what the songs still need after what the day already has, with no second warm-up.
- **Skills.** Every item now names the skills it works on (the altered scale, broken 3rds, rootless voicings, Latin comping, odd meters, and about 50 more), and marking items done rates each skill across every song. The Skills button on the Practice page shows how good you are at each (for example "Level 2, solid"), how many items it's from, and an arrow when it went up in the last two weeks. Plans use it: an item that's basic for you (you're solid at a higher level of all its skills) comes up much less and needs one session, an item one level above your best gets priority, a skill you have counts as a prerequisite met in any song, and tempos start higher where you have the skill.
- **Scale shapes.** Scale work now goes on to shapes once the plain scale is solid: 1-2-3-5 cells, broken tetrads (1-3-5-7), broken 3rds and alternating broken 3rds, broken 4ths, 1-4-7, enclosures, and 1-5-1-3, in continuous scales over the changes, hands together and with left-hand chords under a right-hand scale, and superimposed scales for an "out" sound. A shape item needs its plain scale solid first.
- **7/8 scales.** Heptatonic scale runs that aren't over the song's changes are written in 7/8, one octave a bar. Scales that were in 16ths or triplets are written in eighths at the same speed (the tempo numbers double, or go up by half).
- **Fingerings** on pure scale runs, from Joel's fingering chart (the course books' rules).
- **Shorter text.** Every practice item's goal, steps, and "move on when" were rewritten to the point.
- **The Trip and Live Strong:** the free playing reworked: lines that ignore the chords on purpose (a cell moved by a fixed interval, symmetric shapes, a second key, planing, clusters), landing on the harmony at phrase ends.
- **Everpresent's piano solo** (Solo #1) is in the song as a solo transcription, and its vocabulary (quartal side-slips, triad chains, diminished triads, upper-structure planing, the climax's planed stabs, and more) is in the song's practice items: learn the licks from the transcription, then the same devices on other chords.

### Also in 1.20.0

- **Dark-mode Keys sheets.** Export to Sheets and Demos also writes each Keys sheet in dark mode, in the course books' colours (light music on a dark page), to "Sheet Music/Dark Mode/1 Rhythm/CODE - Keys (Dark Mode).pdf". They don't affect folder colours.
- **Keys braces you set stay.** A Keys sheet of several keyboards kept losing a brace you set across them (Little Louie's Rhodes bass staff with the Hammond treble staff): opening the song put back one brace per keyboard. Braces you set are kept now; a keyboard gets its own brace only when none of its staves has one.
- **Automatic tests:** addsolo:<file>|<first bar>|<last bar>|<name> adds a solo transcription.

## 1.19.0

- **Practice (Home › Practice, below Dashboard).** Practice routines and plans for the songs on a setlist, from practice libraries stored in each song's .starscore:
  - **The setlist.** The Oct 31 setlist is already filled in; Edit to change it (a line like "Set 2" starts a set). Each song shows how much it needs in four areas (parts, comping, harmony, and solos) from 0 to 3; click a number to change it. A song in red has no practice items yet.
  - **One session.** "Make a routine" plans one session of the minutes you choose, starting with a short warm-up, then working through the songs in set order.
  - **Up to a show.** Choose the show date and the minutes for each day (0 for a day off), then "Make a plan". Days give more time to the songs and areas that need more, rotate the songs so each comes back every few days, bring an item back until it's solid, and work up its tempo. The last day before the show is a run-through day, and the show day is a light run of each song. "Plan again from today" plans the days still to come from what you've actually done.
  - **The runner.** Practice (or click any block) opens the runner beside the score. It shows what to play and how, the tempo for today, the exercise's notation, a timer, and the "move on when" test. "Play with backing" opens the exercise in StarScore at today's tempo, with bass and drums where it has them, and plays it; the metronome button turns the click on and off. "Done" records the tempo you reached and how solid it is, which later plans use.
  - **The practice book.** "Practice book (PDF)" writes the whole plan, or one day (PDF beside a day), as a dark book like the course books, with every exercise engraved. It's saved in a Practice folder in your song folder.
  - **iReal Pro.** "iReal Pro charts" writes a page of links, one per exercise with chords, plus a playlist of the day.
  - The practice libraries live in each song's .starscore and are kept when you save the song in any StarScore version. Some memory and seam items show the song's own bars (lead with chord symbols, and your keys staves where they have notes), labeled with their bar numbers.
  - **How a day is planned.** A song's share of the time is 1 plus its four needs plus twice its biggest need, so a song that needs only its solo (Ciao Ferrari) still gets real time. A day works on a handful of songs for about 25 minutes each rather than many songs for a few minutes. Within a song, items come in learning order: the form first, then harmony, technique, ear, and voicings, then parts and comping, then the solo, with each item after any prerequisite planned the same day.
  - **Fast and light.** Reading the song folder unpacks only the start of each score (where the practice data is) instead of the whole score, so a song with no practice items is passed over in about a millisecond, and the folder is read again only for songs that changed. The setlist's needs are kept with the folder index, so the Practice page opens without reading any library. Exercises are kept compressed in memory, about a quarter of the memory they'd take otherwise.

### Also in 1.19.0

- **Faster Scores on export.** Each Score folder's first Score no longer lays out the whole song (every part, in the song's continuous view) when it loads its copy of the song, since nothing reads that layout: it was about 20 s and 1.5 GB of memory per Score folder on G.I. Jorge on the build machine (a few seconds on a Mac). The sheets come out the same.
- **Flexible chairs shown in one step.** When a song opens with its Flexible chairs shown differently from the "Show Flexible horns as" setting, or when you change that setting, every Flexible section is changed in one step, so the song is laid out once, not once per section (G.I. Jorge, three Flexible sections: one layout instead of three when opening in the automatic tests). One undo step too.
- **Timing in the log.** "tidied the opened score" now names the steps that took a tenth of a second or more. With STARSCORE_PRINT_TIMES=1 in the environment, the log also shows where each printed sheet's time goes (layouts, texts, beamed rests, footnote, PDF) and each step of the tighter spacing. The export log says which scores got a 4-Horn 2-Trumpet bracket.
- **Automatic tests:** the export step takes a filter (export:Section Score, or several texts with |) to export only the sheets whose path holds it.

## 1.18.90

- **Part scores lose MuseScore's duplicate numbers.** A part score MuseScore named "Piano 2" or "Electric Piano (1)" to keep it apart from another part score of the same name is renamed "Piano" or "Electric Piano" when you open the song, as long as no other part score has that name any more. The exported sheet's top-left label then reads the right name (The Trip's Keys sheet said "Piano 2").

## 1.18.89

- **Live performance recordings.** In Recordings › Albums, sessions, and originals, the kind dropdown has "Live performance" for a video of a live show that isn't in the filmed-shows list. The Recordings pages list them under "Live performance" (named by the album / who field, or the video's title from a pasted link) and count them with the filmed performances.
- **Oxford commas** in the app's text that was missing them (Recordings, the export dialog, the mutes dialog, New Score, Half-Time, the generated Starsign pages, and a few messages).

## 1.18.88

- **Courtesy accidentals at the barline.** When a note altered at the end of a bar (still sounding in its last beat) comes back in the first beat of the next bar as the key signature has it, that note gets its accidental in parentheses: D♯ then D in C gets (♮), E♮ then E♭ in E♭ gets (♭). Same octave only. A figure that repeats gets them only the first time (within 16 bars). Hidden notes and rhythm slashes don't count. It works in the app as well as on exported sheets, and each song gets them the next time it's opened. To hide one, make it invisible.
- **Flexible sheets spelled like your hand-written parts.** When a Flexible version sheet is exported for a transposing instrument, each bar is looked up in the song's part scores in the same transposition (the Standard arrangements' parts). Where the same music is found (same rhythm, same notes in any octave, same key), its spellings and its hand-placed accidentals and parentheses are used: Little Louie's 3H and 2H Flexible Horn 1 B♭ sheets now match the Standard Trumpet, (♭)s in section L included. A match at the same place in the song is used first. A match elsewhere in the song counts only when a bar next to it matches too (or the bar is long). Pitches never change, only how they're spelled.

## 1.18.87

- **Bars' "Break multimeasure rests" setting is copied with the layout.** Wherever StarScore copies a part score's layout to another (a Flexible arrangement made from the Standard one, copied horn parts, stand-in versions, + Voice sheets, combined sheets, standardizing), each bar's "Break multimeasure rests" setting (Measure properties) now comes along with the line breaks and system locks.

## 1.18.86

- **Newer default style offered.** When you open a song whose main score doesn't match the default style yet (a new built-in Starsign style, or a new default style you picked), StarScore asks once whether to apply it to the main score and every part score. Yes restyles the song (one undo step per score). No isn't asked again for that song until there's an even newer style. StarScore's own per-score settings (staff size, footers, title offsets, multimeasure rests) don't count as differences.
- **Texts beside rehearsal marks.** A staff or system text at a rehearsal mark's beat is placed beside the mark, on the same line, instead of being stacked under it (G.I. Jorge's "Soloist plays over backgrounds throughout solo" at F). Texts you've moved by hand keep their place.

## 1.18.85

- **Copyright year.** The StarScore bar's "…" menu has "Copyright year (2022)…" to set the year in an original's copyright line by hand. It's kept in the song and every score's line changes with it. Covers have no year, so the item is greyed out for them.

## 1.18.84

- **Release notes show in full.** The update window cut 1.18.83's notes off at its first word in angle brackets, which it took for formatting. Angle brackets now show as written.
- **From 1.18.83, in short:** a copyright line at the bottom of every sheet's first page ("© 2022 Connor Stratman, Joel McCullough, and Starsign. All rights reserved. …", or for a cover "“Bumper Cars” by Alfa Sekitoleko. All rights in the composition belong to its copyright owners. …"); visibility and parentheses follow between the main score and the part scores, accidentals included; Keys part scores keep only each keyboard's own brace and the first system is indented; combined sheets keep a full beat where a slash is hidden; tab names have even space on both sides.

## 1.18.83

- **Copyright line.** The first page of every sheet now carries a copyright line at the bottom, centred, in small type. An original reads like "© 2022 Connor Stratman, Joel McCullough, and Starsign. All rights reserved. No part of this music may be copied, arranged, recorded, or distributed without permission." A co-written song lists the other composers first, then Joel McCullough, then Starsign; a song by Joel alone names only him. A cover reads like "“Bumper Cars” by Alfa Sekitoleko. All rights in the composition belong to its copyright owners. Arranged by Joel McCullough for Starsign’s own performance use. Not for sale or distribution." Arrangers ("arr. …", "Horns arr. …") are left out. The year is the song's first performance or release in the recordings, else its first export, and is kept once a sheet has been exported. The line wraps into even lines between the footnote on the left and the version on the right, or sits above them when there isn't room, and the music keeps at least 2.5 mm from it.
- **Visibility and parentheses follow between the main score and the part scores.** Making something invisible, or putting it in parentheses, in the main score or in a part score now does the same in the other. Accidentals' parentheses and brackets follow too.
- **Keys part scores: names and indent.** A Keys part score keeps only each keyboard's own brace (Make Keys also added one round all the keyboards, which pushed the staves right). A part whose top staff is hidden as empty still gets room for its name on the first system, so the first system is indented.
- **Combined sheets: beat slashes over notes.** Only the slash's notehead is hidden, so a beat that had a hidden slash keeps its full width (Bumper Cars E1).
- **Tabs.** The space right of a tab's name matches the space left of it.

## 1.18.82

- **Keys tab.** A Keys part score (the Rhythm Section's keyboards in one, made with Make Keys part) now opens with its section like any one-instrument part score, and the "Section scores visible" switch no longer closes it. Before, a song's Keys tab could disappear when the song opened and wouldn't come back.

## 1.18.81

- **Rhythm markings: rests.** "Rhythm markings above/below the staff" and "back to normal notes" now work on rests too, and the Rhythm markings menu shows when you right-click a rest. Rests beside rhythm markings are cue size like the markings: making markings makes the rests in those bars small, selected rests are made small, and in a bar of rests only they're moved beyond the staff on that side. Back to normal makes them full size and puts them back. Rests beside existing markings are made small when a song opens.

## 1.18.80

- **Respace lyrics on a part score's tab.** Right-click the tab of a part score with lyrics and choose Respace lyrics: the syllables get a word space between them, and where they're still squeezed their bars are widened and lines split in two, without resetting the part score. A message says what changed; you can undo it.

## 1.18.79

- **Dynamics keep a full space from the notes above or below them.** A dynamic under low notes (or their stems, beams, accidentals and articulations) used to sit half a space under them, right against them; now it's a whole space. Its distance from other things (hairpins, text) is unchanged. Double Entendre 6H Tenor Sax: the mf under the low D in bar 74, the f under bar 41's beam.

## 1.18.78

- **Lyrics on "+ Voice" sheets get room.** A + Voice sheet (Bass + Voice, Lead Sheet + Voice…) keeps its part score's lines, but now asks for a word space between syllables (0.7 sp, not a quarter of a space), so the bars with lyrics come out wider. Where a line still squeezes the lyrics, StarScore widens their bars, and if that isn't enough splits the line in two (4 bars become 2 + 2). This runs every time the sheet is formatted, at every export. Ghostbusters' Bass + Voice: "I ain't 'fraid of no ghost" instead of "Iain't'fraid ofnoghost", still 4 pages.

## 1.18.77

- **Reset part score… on a part score's tab.** Right-click a part score's tab and choose Reset part score…; after you confirm, it's made again from the main score, as the (hidden) Parts dialog's Reset does: its title frame comes back, and what was set in that part score only is lost. StarScore then gives it the Starsign style, its sheet title and mutes again. You can undo it.

## 1.18.76

- **Falls (and other lines after a note) start next to the notehead when the note has an accidental.** MuseScore pushed them right by the accidental's width, a parenthesized accidental most of all (Double Entendre 6H Trumpet 1, bar 57).

## 1.18.75

- **System text no longer breaks multimeasure rests, unless you tick it to.** Select a system text and tick "Breaks multimeasure rests" in its Properties (off by default). A text that doesn't break a rest is drawn on the rest where its bar falls ("End solo 1" on the last of 8 bars shows near the right end of the 8-bar rest).
- **Lead Sheet + Voice (and every "+ Voice" sheet):** the Voice staff is full size, the Voice and the instrument are in one bracket, and rehearsal marks, tempo and system texts are above the Voice. A "+ Voice" sheet holding instruments its part score doesn't (Ghostbusters' had the Congas, so it was never formatted and some rehearsal marks didn't show) is made again.
- **Triplet brackets clear the slurs** on the same staff (Break Out, bar 43).

## 1.18.74

- **Mutes show on the part score as soon as you set them, in a song that hasn't been exported yet.** A song not yet in Sheets and Demos had no sheet titles on its part scores (instrument name, arrangement, mutes) until its first export; they're now worked out as if it were there.

## 1.18.73

- **Starsign 2.9: all lyric verses in Futura.** Verses 2 and 4 were still in Jost; now every verse is Futura. The installed Starsign 2.9.mss is replaced again; songs take it when you Apply styles or Update all sheets.

## 1.18.72

- **Starsign 2.9 style: Joel's lyric settings.** Lyric hyphens are no longer forced between syllables that are close together, hyphens are thicker (0.23 sp), and verse 1 and 3 lyrics are in Futura. The built-in Starsign 2.9.mss is replaced (the earlier built-in 2.9 isn't kept beside it). Songs take it when you Apply styles or Update all sheets.

## 1.18.71

- **Recordings PDFs separate Sweater Weather shows from Starsign shows again.** Each live take now goes with the band that played the show. Ciao Ferrari's page had listed 17 Sweater Weather shows as Starsign shows, and Intern's page all 8 of its shows; they're now under "Sweater Weather, live". Every song's Recordings PDF and All Recordings.pdf are rebuilt once on the next organizer run.

## 1.18.70

- **Fixed a crash when clicking a chord symbol with parentheses in a part score.** When parentheses were added to a chord symbol, the copies in the part scores were put on the main score's staff instead of the part score's. Clicking one crashed StarScore (twice in Ciao Ferrari's Bass and Guitar part scores). New parentheses now go on the right staff, songs saved with the wrong staff are repaired when they open, and selecting an item on a missing staff no longer crashes.
- **A new 1-Horn takes the 2-Horn's Trumpet.** When you create the 1-Horn arrangement and the song has a 2-Horn whose Trumpet has music, that music is copied into the 1-Horn Trumpet without asking, with its part score's formatting and text positions, marked Sketch.
- The message after the 1-Horn sheets are made no longer says the B♭ Saxophone is an octave lower (only the Trombone is).

## 1.18.69

- **Every Rhythm Section instrument with music gets a part score.** When a song opens, a Rhythm Section instrument that has notes but no part score of its own (and isn't in one with others, like Keys) gets one, named as its sheet: Drums, Keys, Guitar 2, Bass 2… It's styled like the others. Ciao Ferrari gets Drums and Keys. Other songs that had gaps: Deimos (Guitar 2, Bass), Fish Oil (Drums), Intern (Keys), Pick Up The Pieces (Drums), Porcupine (Guitar 2, Keys, Bass 2, Bass Drum). Horn and string parts without a part score are left as they are.

## 1.18.68

- **Composers follow the main score.** Changing the composer (or lyricist) text in the main score now changes it in every part score too, in the same undo step. In many songs the part scores' composer texts weren't linked to the main score's (Ciao Ferrari: 12 of 20), so they kept the old names. The part scores are also brought in line when a song opens and before each export.
- **Rehearsal marks on multimeasure rests at the start of a line** are placed like the others. Ciao Ferrari's M (Trombone 1, a 4-bar rest at the start of the last line) sat left of the clef, far left of K and L.

## 1.18.67

- **Projects and Sheets' top folders numbered from 1.** The next folder organization renames them once: STARSIGN → `1 Starsign`, `4 Other Projects` → `2 Other Projects`, `6 Templates` → `3 Templates`, `7 Sketches` → `4 Sketches`, `8 Quarantine (empty template copies)` → `5 Quarantine (empty template copies)`, `9 Inbox` → `6 Inbox`, and the `10 Sweater Weather` shortcut → `7 Sweater Weather`. The folders inside `1 Starsign` (Originals, Covers, Vocal Covers…) don't change. A song that's open follows its folder. Everything in StarScore that uses these folders (Move to…, filing, the Inbox, new songs, the reports) uses the new names.

## 1.18.66

- **Rests in beams no longer sit on accidentals on screen.** A rest inside a beam is moved up or down to clear the beam after the bar is spaced, so it could land on the next note's sharp or natural (or a grace note's) or the note before it. The printed sheets already fixed these by widening the bar; the score on screen now does too:
  - the rest moves sideways into the free space between the notes either side, when there's room;
  - otherwise its bar is laid out a little wider on screen (up to twice as wide, as on a printed sheet). This isn't saved and doesn't change your file;
  - if even that isn't enough, the rest moves away from the beam until it clears.
  - Seagrass had 90 of these across its scores, G.I. Jorge 183, Drum Battle 38, Bet 12; all now 0. Printed sheets come out as before.
- Grace notes' accidentals now count when the printed sheets check rests in beams, too.
- **1-Horn Bb Saxophone.** Made from the 1H Trumpet sheet, it now plays at the Trumpet's pitch (written an octave above the Trumpet's notes) instead of an octave lower. Parts already made keep their notes. The Trombone still plays an octave lower.

## 1.18.65

- **Rhythm markings back to normal notes.** Next to "Rhythm markings above the staff" and "…below the staff" (Format menu and the right-click menu) there's now "Rhythm markings back to normal notes". It makes the selected markings normal notes again: normal size, played, normal noteheads (slashes become normal heads; diamond and x heads stay), stems automatic, and back at their own pitch. It works in the full score or a part score, and changes the notes in every score they appear in. Normal notes in the selection are left alone.

## 1.18.64

- **Vocal covers have a Voice.** The Voice is a section of its own (one "Voice" staff in treble clef, at sounding pitch, under the lead sheet in the main score) with its own status.
  - A new Vocal Cover (File › New, Type: Vocal Cover) starts with it. For a song already in Vocal Covers (Ghostbusters, Another Star, Move On Up), add it with "+ New section" › Voice. Other songs don't offer it.
  - Once the Voice is marked Finished, the export writes `CODE - Voice.pdf` into the lead sheet's folder, which is then called `1 Lead Sheet + Voice` (the lead sheet moves along with it).
- **Lead Sheet + Voice.** Once the Voice and the lead sheet are both Finished, StarScore makes a "Lead Sheet + Voice" part score: the Voice in a small staff on top, the lead sheet below, with its chord symbols and texts. It's exported as `CODE - Lead Sheet + Voice.pdf`.
- **Add Voice to a part…** (StarScore bar, ⋯ menu) makes a part score like "Bass Guitar + Voice": the Voice in a small staff above that part. It's exported beside the part's own sheet, named after it (`1 Rhythm/CODE - Bass + Voice.pdf`), once the Voice is Finished. The instruments are named beside the staves, without their section ("Tenor Sax", not "3H: Tenor Sax").
- Every "+ Voice" part score takes its part score's formatting again at each export: its style, system breaks, system locks, text positions and bar widths. Page breaks aren't copied: with the Voice above, a page of the part's systems doesn't fit on one page, so the systems fill the pages as they come.
- Marking a "+ Voice" part score Finished marks the part under the Voice; the Voice keeps its own status.
- **Fewer sheets rewritten for nothing.** Sheets are compared with the ones already in Sheets and Demos with a tiny allowance for rounding (0.003 pt). Before, a note moved by a ten-thousandth of a point could count as a change, archive the old sheet and write it again.

## 1.18.63

- **Tenutos and staccatos no longer sit on a tie.** On the second of two tied notes, an articulation on the tie's side could land on the tie, most of all on a note on the middle line (its tenuto went in the space above, where the tie's arc is). This was a MuseScore bug: it placed the articulation before laying out the tie and only guessed how much room the tie needed. Now, once the ties are laid out, such an articulation moves away from the note until it clears the tie: a space at a time inside the staff, a little at a time outside it.

## 1.18.62

- **Projects and Sheets has a STARSIGN folder.** The Starsign songs move into it the next time the organizer runs (after an export, or ⋯ › Run folder organization…):
  - `STARSIGN/1 Originals`: `1 Repertoire`, `2 In Progress` (Mxter Shirts, Intern, Big Milk, Jeju, Meevin) and `3 Sketches` (the rest of the old WIP folder).
  - `STARSIGN/2 Covers`: `1 Repertoire` (the covers that have a folder in Sheets and Demos) and `2 In Progress`.
  - `STARSIGN/3 Vocal Covers`: `1 Repertoire` and `2 In Progress` (Ghostbusters, Another Star, Move On Up).
  - The old `4 Other Projects/Mxter Shirts` goes into the Starsign Mxter Shirts' `MuseScore Files`. 4 Other Projects, Templates, Sketches and the Inbox stay where they are, and Sheets and Demos doesn't change.
  - If the open song is moved, StarScore saves it at its new place.
- **New Starsign scores are saved and filed right away.** File › New has a Type choice: Original, Cover or Vocal Cover. On Create, the song gets a code and is saved as `CODE - Title.starscore` in its own folder: originals in Originals › Sketches, covers and vocal covers in their In Progress folder. Scores that aren't Starsign scores work as before.
- **Move to…** in the StarScore bar's ⋯ menu moves the open song (its whole folder) to another STARSIGN folder, e.g. from In Progress to Repertoire.
- When a song is first exported to Sheets and Demos, its "Kind of song" is picked from its STARSIGN folder (a vocal cover: "Cover that needs a vocalist"; an original not yet in Repertoire: "Work in progress"). You can still change it.

## 1.18.61

- **Resizing the Reference PDF panel is smooth.** Every time the panel got about 100 pixels wider or narrower (50 on a Retina screen), StarScore drew every page of the PDF again on the spot, holding up the whole app while you dragged. Now the pages already drawn are just stretched while you resize, and the sharp ones are drawn in the background a quarter of a second after you stop. The same goes for zooming.

## 1.18.60

- **Rests in beams stay in line after an edit.** Moving something on a page (a staccato, a text) could leave the beamed rests in the first bar of a later line at their plain height, out of line with the bars around them (Bumper Cars, 3H Bass Clarinet, bar 11). This was a MuseScore bug: when it re-laid out only part of the page, it redrew that bar's beams without moving their rests clear of the beam. Exported sheets weren't affected (an export lays out the whole sheet); it showed while editing.

## 1.18.59

- **4-Horn 2-Trumpet without a 5H.** In a song that has a 3H Standard but no 5H, making the 4-Horn 2-Trumpet no longer asks you to create the 5H first. Its Trumpet 1, Tenor Sax and doubler are copied from the 3H Standard (Trumpet 2 starts empty). With a 5H, the Trumpets still come from the 5H.

## 1.18.58

- **Rests inside a beam no longer sit on the next note's accidental.** MuseScore moves a rest in a beamed group up or down to keep it clear of the beam, but only after the notes have been spaced, so the rest could land on a sharp or flat beside it (Last Pint's Tenor Sax, bars 48 and 128, and many others). Now:
  - In the score, such a rest keeps moving away from the beam to the first spot clear of the notes on either side, as long as it stays within a space of the staff.
  - Before a sheet is printed, any beamed rest still touching a note is moved sideways into the free space beside it. If there's no room, its bar is made a little wider. In a bar too crowded for either, the rest goes just above (or below) the accidental, unless a chord symbol is in the way.
  - The export log lists any that are left ("rest on a note").

## 1.18.57

- **The 4-Horn 2-Trumpet's horns are bracketed.** Its four horns are in one bracket in the main score and in the 4-Horn 2-Trumpet arrangement's score, like the other Standard sections. Songs that already have the section get the bracket when they're opened (save the song to keep it); a new 4-Horn 2-Trumpet gets it when it's made.

## 1.18.56

- **Shorter mute lines when they run into the title.** The mute lines under the instrument name are shortened too: "with Harmon Mute" → "w/ Harmon Mute" → "w/ Harmon", "and Plunger" → "& Plunger". Whichever line reaches furthest right (the name or a mute line) is shortened first.

## 1.18.55

- **Shorter instrument names when they run into the title.** The instrument name top left is shortened one step at a time until it clears the title: "Soprano Saxophone" → "Soprano Sax" → "Sop Sax" → "Sop", "Trumpet 1 in B♭" → "B♭ Trumpet 1" → "Trumpet 1" → "Tpt 1", and the same for the other horns and strings. A name with no shorter form is still broken over two lines. Mute lines under it stay as they are.
- **Fixes from the code audit:**
  - The autosave archive never deletes a copy it hasn't archived. When the clocks go back in November, copies made in the repeated hour would have been lost. A copy that can't be read is kept as ".unreadable" instead of deleted.
  - Saving never rewrites the song file with a part of it that couldn't be read back; the file stays as saved.
  - If the song's StarScore data is ever damaged, StarScore leaves it in the file instead of writing empty data over it.
  - Audio demos: the old demo is removed only after the new one is in place.
  - A failed export no longer raises the song's version number (nothing was printed at that version).
  - Undo after removing an arrangement brings back both the arrangement and its score (before, only the score came back). Renaming horn parts (Standardize, new sections) can be undone too.
  - Standardize and "detect sections" make the 4-Horn 2-Trumpet arrangement again if a song has its section but not the arrangement.
  - The autosave history of a song follows it when the organizer gives it its code ("Title" → "CODE - Title"); before, the coded song started a new history.
  - Saving the band roster keeps anything added to roster.json by hand.
  - Double-Time: markings with no bar to go to are freed (a small memory leak).
  - The pre-print layout checks are faster on big Scores (same results).

## 1.18.54

- **Sheets a song no longer makes are archived.** When a part is put on another instrument, a section is deleted, or a sheet is renamed, the export moves the sheets it wrote before that it no longer writes into Version History, and the old folder goes once it's empty. For example, Amplitudes' 4-Horn 2-Trumpet first made with the doubler on Clarinet left "4H 2Tpt Cla Ten" beside the new "4H 2Tpt Alt Ten". Only sheets StarScore itself exported are moved; anything else in the folder keeps it in place.
- **Mute lines are smaller.** The "with Harmon Mute" lines under the instrument name top left are now the same size as the "with 2 trumpets" line under the arrangement name.

## 1.18.53

- **Show all / Hide all.** Two buttons after "+ New section" on the StarScore bar show or hide every section at once.
- **Combined Parts look tidier.**
  - Their texts sit where the style puts them. The sheet reused the largest arrangement's part score, with text positions set by hand for that layout, which put Amplitudes' "Chords provided to help y'all remember accidentals" off the left of the page.
  - The arrangement labels ("2H / 4H–7H / 3H", "All") are smaller and start at the note instead of being centred on it.
  - Labels on one line of music sit at one height instead of following each bar's notes up and down like a staircase.
- **Every sheet is checked before it's printed** (parts, Section Scores, Combined Parts, Double-Time and Half-Time):
  - A text wider than the page is broken over two or more lines.
  - A text or rehearsal mark running off the left or right edge starts (or ends) at the note instead of being centred on it, or is moved in to the margin. A chord symbol at the end of a line is moved in too.
  - A text, tempo marking or rehearsal mark printed over a chord symbol or a bar number is moved clear of it ("Play as written or improvise, your choice" in Amplitudes' Section Scores and Combined Parts). So is a Combined Parts label over another text.
  - The same text twice almost on top of each other is printed once.
  - Texts you placed by hand that overlap each other are left as they are and listed in the log.
  - The part sheets are printed from your part scores, so these changes are made there too and stay if you save the song.
- **Ciao Ferrari's export no longer crashes.** A first bar that starts a repeat crashed the export while it made the 2H Flexible Section Score.

## 1.18.52

- **Change an instrument.** In a horn section's menu on the StarScore bar, **Change an instrument ▸ (part) ▸ (instrument)** puts that part on another horn: Trumpet, Piccolo, Flute, Clarinet, Soprano, Alto, Tenor or Bari Sax, Bass Clarinet, Trombone, Bass Trombone or Tuba. The music stays at the same sounding pitch and is written for the new instrument. The part and its part score are renamed ("4H 2-Trumpet: Alto Sax", "Alto Saxophone" on the sheet), its sheets go in the folder named for the new instruments, and mutes or a flugelhorn mark that don't fit the new instrument are dropped. Undo takes it back. Use it when a section was made with the wrong doubler instead of deleting and remaking it.
- **The doubler's recommendation follows the right sections.** A new 3-, 4- or 5-Horn (or 4-Horn 2-Trumpet) recommends what the doubler plays in the song's other 3- to 5-Horn sections, and a new 6- or 7-Horn what it plays in the song's 6- and 7-Horn. Before, a song whose doubler plays Alto Sax in its 3-, 4- and 5-Horn and Clarinet in its 6- and 7-Horn (Amplitudes) was offered Clarinet for a new 4-Horn 2-Trumpet.
- **The two 4-Horn arrangements say which they are.** Under "4-Horn Arrangement" at the top right of their sheets and Section Scores, a smaller second line says "with 2 trumpets" or "with trombone".
- **Green needs both 4-Horn arrangements.** A song folder is green (and so blue or purple) only with the 4-Horn with trombone and the 4-Horn 2-Trumpet. Run folder organization recolors the folders straight away: a song whose sheets were last exported before 1.18.52 can't have a 4-Horn 2-Trumpet, so it drops to yellow until it has one and is exported again.

## 1.18.51

- **G.I. Jorge's Double-Time export no longer crashes.** Making the Double-Time (and Half-Time) sheets replaces the song's bars in a copy. The old bars were thrown away while the part scores still pointed at pieces of their ties, slurs and lines (G.I. Jorge's rit. line among them), so laying out a part score afterwards wrote into memory that was no longer in use. On a Mac that crashed StarScore a little later, partway through the Double-Time sheets. The part scores are now laid out with the new bars before the old ones are thrown away.
- **New arrangement: 4-Horn 2-Trumpet** (Trumpet 1, Trumpet 2, the doubler, Tenor Sax). It's in the StarScore bar's Add arrangement menu. The doubler takes the alto chair, as in the 3-, 4- and 5-Horn, and is chosen the same way: Alto Sax is recommended, or whatever the doubler already plays in the song's other horn sections. When it's made, its Trumpet 1 and Trumpet 2 are copied from the 5-Horn and its Tenor Sax and doubler from the 3-Horn Standard, notes and part score formatting (the copies are marked Sketch, as other copied parts are). If the song has no 3-Horn Standard or 5-Horn yet, or their horns aren't marked Finished, StarScore says "It is recommended that you create 3H Standard and 5H first." with Continue, Cancel, and Create 5H / Create 3H for the ones that don't exist yet. Its sheets go in a folder named from its instruments ("4H 2Tpt Alt Ten"), its parts are named "4H 2-Trumpet: Trumpet 1" and so on, and it has its own dashboard column ("4T"). It's needed for a song's folder to be blue.
- **Copied parts keep their hidden notes and rests.** Copying parts into a new arrangement, and Copy layout, now also copy which notes and rests are hidden in the source's part score. Before, a slash hidden under a whole note in the 5-Horn's Trumpet 1 sheet showed on the copy.

## 1.18.50

- **G.I. Jorge's export no longer crashes.** A laissez vibrer ("let ring") tie on a staff that the export shows while setting up a Score, in a bar that was part of a multi-bar rest, crashed StarScore at the 7-Horn Section Score.
- **Recordings: album / who and date fill in from the YouTube link.** In Recordings › Albums, sessions, and originals, pasting a YouTube link reads the video and fills in the album / who and the date, unless you typed them yourself. For a song from a YouTube "Topic" channel (the ones that say "Provided to YouTube by…"), it uses the artist, the album and the release date: an original is named "The Meters" or "Herbie Hancock — Head Hunters", an album by the album's name. Other videos give the channel's name for an original and the video's title for an album or a session, with the date it was posted. Changing the kind afterwards names it again.
- **Double-Time and Half-Time sheets use their standard sheet's multi-bar rest setting.** Each part score's Double-Time and Half-Time sheets have multi-bar rests if its standard sheet does (nearly always). G.I. Jorge's Double-Time sheets from 13 October have none because they were made before 1.18.47; exporting them again makes them with multi-bar rests.

## 1.18.49

- **Modes as chord symbols.** Type a root and a mode name without spaces, and the chord symbol shows it spaced: "Blocrian" shows "B locrian", "Charmonicminor" "C harmonic minor", "Daltered" "D altered", "F#mixolydianb6" "F♯ mixolydian ♭6". The root is an ordinary chord root, so it changes when the chord is pasted onto a transposing instrument (B locrian on a concert staff becomes C♯ locrian on trumpet) and in transposed parts. The names: the seven modes of the major (natural minor), harmonic minor and melodic minor scales under their usual names (ionian, dorian, phrygian, lydian, mixolydian, aeolian, locrian, natural minor, harmonic minor, melodic minor, jazz minor, phrygian dominant, lydian dominant, lydian augmented, ionian augmented, altered, superlocrian, ultralocrian, Ukrainian dorian, aeolian dominant…), any of them followed by alterations ("locrian#6", "dorianb2", "lydian#2", "mixolydianb9b13", "superlocrianbb7"), and half-whole, whole-half, diminished, augmented and whole-tone ("Chalfwhole" shows "C half-whole"). Capitals and hyphens don't matter. "alt", "dim", "aug", "minor" and "major" on their own are still chord qualities (C7alt, Cdim7). Chord charts write the mode out too; the iReal Pro page uses the mode's seventh chord (B locrian as Bø7) and lists it in its notes.
- **New scores open their part scores.** Making a new StarScore, or adding a section, opens the new part scores' tabs, as showing a section does. Before, every section had to be hidden and shown again.
- **Section breaks stay in every part score.** A section break is in the full score and every part score. Locking systems in one part score (or StarScore copying a part score's layout, which locks its systems) took away every section break inside the locked bars, in every score; the lock is now split at the section break instead. Adding a section break also takes away the locked system around it in every part score, not only in the one it was added in, so the bars around it are laid out the same way everywhere.
- **Expression text 9 pt.** The default part-score style is now Starsign 2.9, with expression text at 9 pt (was 8 pt). New scores and sections get it; for existing songs, apply the part styles again.
- **Windows build fixed.** 1.18.48's Windows build failed on the macOS-only update check.

## 1.18.48

- **Opening StarScore during an update.** While an update is being installed, opening StarScore shows the "Installing StarScore Studio" window ("Installation in progress. StarScore Studio will open by itself once the installation is finished.") instead of starting the old app. The window closes and StarScore opens when the install is done. This works from the next update on: the update to 1.18.48 is still installed by 1.18.47's installer.
- **Safer installs.** StarScore only waits for an install that is really running: a lock file left by an install that was stopped, is more than 20 minutes old, or belongs to another program is ignored and deleted, so it can never keep StarScore from opening. If an install is stopped partway, the old app is put back and the half-copied one removed. The downloaded app's quarantine flag is cleared before it goes into Applications.

## 1.18.47

- **Double-Time and Half-Time sheets get their own folders.** They go in "Sheet Music (Double-Time)" and "Sheet Music (Half-Time)" next to "Sheet Music", with the same arrangement folders inside. Sheets already in "Sheet Music/…/Double-Time" or "Half-Time" folders are moved there the next time the song is exported. Where a sheet is in both places, the newer one is kept and the older one goes to Version History.
- **Double-Time and Half-Time sheets follow the standard sheets' lines.** These sheets keep the song's bar numbers, and each line starts at the bar number a line of the standard sheet starts at, so each line holds the same music as on the standard sheet. If the standard sheet has a line every four bars in section A, so does the Double-Time or Half-Time sheet, counted by bar number. Where that much music doesn't fit on one line (usual on Double-Time sheets, which have twice as many bars), the line is split into two (or four) lines at equal bar numbers, so lines still start at the standard sheet's bar numbers. On a Half-Time sheet, a line that would start on the second half of a bar starts at that bar. Exporting these sheets takes a little longer, since each part is laid out once or twice more.
- **Bar numbers only at the start of each line** on Double-Time and Half-Time sheets. Before, the second bar of a line was often numbered too.
- **Multi-bar rests on Double-Time sheets.** Bars of rest are joined into one multi-bar rest, as on the other sheets. Before, every bar of rest was written out.
- **Chord charts: sharps and flats no longer touch the number after them.** The ♯ in F♯7 sat on the 7. Sharps are now smaller and raised.

## 1.18.46

- **Flugelhorn parts.** Format › Flugelhorn part on/off (also in the StarScore bar's menu) marks the trumpet part on screen as played on flugelhorn. Its sheets then say "Flugelhorn" top left, and the file names still say Trumpet. When both trumpets in an arrangement are flugelhorns, the sheets say "Flugelhorn 1" and "Flugelhorn 2". When only one is, it says "Flugelhorn" and the other says "Trumpet in B♭". A Flexible chair can be marked too: its trumpet sheet then says "Flugelhorn". Copying a flugelhorn part to a trumpet part in another arrangement copies the setting.
- **Chord charts rebuilt for reading at sight.**
  - Four bars to a line, every line starting on a 4-bar phrase. A section's bars line up in columns, all the same width whatever their time signature (six bars of 3/4 used to share a line). One bar left over joins the last line; two or three make a short line.
  - A section whose chords don't fit four bars to a line at a readable size gets two bars to a line instead. Each section has one chord size, so sizes no longer jump from bar to bar.
  - Every rehearsal mark is its own section. A long section the same as the one before says "Same as D"; a short one is written out again. "D–F 3 times" over a "2×" repeat is gone.
  - Bars without chords are one multi-bar rest even when the time signature changes inside them (Drum Battle's drum intro used to show as empty bars).
  - A 2nd ending sits under the 1st, with room for its bracket.
  - Labels, counts, time signatures, the form line and the notes over each section are in the chord font, bold and larger. Only the title keeps the display font.
  - The chart is laid out once its fonts have loaded, so the chords are measured in their own font.
- **Combined sheets cleaned up.**
  - Arrangements that play the same notes share one line. Before, small differences that don't print, such as a note muted in playback, a dynamic's playback volume, a beam's angle or where a slur into the bar started, split them into two voices playing in unison (Drum Battle's trumpet, letter B). Different dynamics alone no longer split them either; the line keeps one arrangement's.
  - Lines split only in the bars where they differ, not for the whole phrase. A single bar where two lines come together between two passages split the same way stays split, so lines don't join and split bar by bar. Where three or more lines come together, the bar is one line.
  - Lines in the same rhythm share stems: one chord, labelled over it top line first ("1H / 2H / 3H"). A unison is one note.
  - Arrangements that rest get no line and no label, so "4H–7H tacet" and empty voices of rests are gone. The label says who plays.
  - At most two lines to a staff. Three or four different lines get a staff each, shown only where needed.
  - Notes tied from one passage into the next keep their ties.
  - Beat slashes that overlap another line's notes are hidden; the notes stay. Rhythmic slashes (with stems) are never hidden.
  - "(open)" and "open" count as the same text. A dynamic is kept on each staff.
  - The top left lists every mute any arrangement uses. A chair played on flugelhorn in only some arrangements says "Trumpet".
- **One bass guitar part.** When a song with a 4-string and a 5-string bass part opens, the two become one "Bass Guitar" part with the 4-string part's notes. If the 4-string part is empty, the 5-string part's notes are kept. The song as it was is copied first to Version History/<date> Before one bass part. Save the song to keep the change. Treble-clef, 6-string and other second bass parts are left alone.

## 1.18.45

- **Rests beside rhythm markings place themselves.** In a bar where a voice holds rhythm markings, its rests sit beside them: 2 spaces above the staff for markings above it, 2 spaces below for markings below. That holds in the full score, every part score and every exported sheet, the Flexible ones included. Rests you had moved by hand there lose that move once, when the song opens (Drum Battle: 524 rests), so they aren't moved twice.
- **Export crash fixed.** Setting the new version number in every part score's footer could crash after a long editing session (Drum Battle, 14 Oct). The footer is now set without the step that crashed.
- **One bass sheet.** Bass sheets named after a player from before StarScore ("Bass (Mike)", "Bass (Harley)") go to Version History once the song's Bass sheet is exported.

## 1.18.44

- **Rhythm markings above / below the staff works in part scores.** It always said "Select the notes to make rhythm markings first" in a part score, because it looked at the selection in the full score. It now uses the score on screen, and the full score and the other part scores get the same change (size, playback and stem direction included).

## 1.18.43

- **Rhythm markings stay on their space.** The small, silent slash (or diamond, or x) notes that show a rhythm above or below the staff (Drum Battle, letter L) are now pinned to the first space above the staff, or the first space below it, whatever their pitch. That's how MuseScore's own slash notation stays on the middle line. Copying and pasting them onto another instrument, changing the instrument, transposing, the Flexible view and StarScore's own part copies no longer move them.
- **Existing markings are pinned when a song opens.** Each one goes on the side it already shows on.
- **New command: Format › Rhythm markings above the staff / below the staff** (also on a note's right-click menu, under Rhythm markings). It makes the selected notes rhythm markings in one step: small, silent, slash heads, stems up above the staff or down below it, and pinned.

## 1.18.42

- **The multimeasure rest number counts bars in an additive time signature.** Six bars of rest in (4+5)/4 now show one multimeasure rest numbered 6 (1.18.36 showed 3, one per composite bar). The rest still starts at the start of the pattern and holds whole patterns only, and the "minimum number of empty measures" setting now counts bars too.

## 1.18.41

- **The soprano clef no longer pushes staves apart.** It sits on the bottom line, so it hangs about two spaces below the staff. That overhang is now left out of the spacing between staves, so the Flexible Horn 2 part in the "range clefs" view takes the same room as it does on the exported sheets.
- **Mutes are copied onto Flexible parts.** A new Flexible section filled from the Standard section takes each part's mutes, and copying parts into a Flexible section copies them too. Copying from a Flexible part to a brass part copies the mutes that instrument has.
- **The mute lines under the instrument name follow.** After parts are copied, every part score's title is brought up to date, so the "with Harmon Mute" lines appear with the copied mutes. A Flexible chair's part score shows its mutes under "Horn 1" too, and a Flexible sheet made into a part of its own shows its chair's mutes.
- **No mute text on woodwind sheets.** Every text that is only a mute or open marking ("harmon", "(plunger)", "Harmon (stem out)", "w/ plunger", "to cup", "con sord.", "+ o"…) and the brass mute symbols on notes are left off the exported sheets of saxophones, clarinets, flutes and the other woodwinds, and off the Flexible sheets for instruments without a mute. The part scores keep them. "Open solos" and "straight 8ths" stay.

## 1.18.40

- **A "Sheet Music" folder in every song folder.** Export to Sheets and Demos now puts every sheet folder inside the song folder's **Sheet Music** folder. That covers 1 Lead Sheet, 1 Rhythm, the horn folders, Extras, String Duo and the like, Combined Parts and Annotated Sheets. Chord Charts, Demos, Reference PDFs, Horn Part Guides, Update Notes and Version History stay at the top of the song folder, next to the What's Here and Recordings PDFs.
- **Existing songs are moved over by themselves.** The next folder organization moves every song's sheet folders into Sheet Music, and an export does the same for its song first. Nothing is made again, and the folder colours, the changelogs, the export ticks and the version suggestions carry over. If an older StarScore later exports to the old places, the next organization moves those files in too; of two files with the same name, the older one goes to Version History.
- What's Here and the Band Guide show the new layout.

## 1.18.39

Overnight bug and speed pass.

- **The To-Do PDF's "parts finished" bar shows the Finished share again.** It went missing in 1.18.38.
- **Faster exports.** Combined parts (such as Keys) are made about four times faster, and a full export of a big song takes about 15% less time (Last Pint: 10½ minutes, down from 12⅓).
- **Faster part score tab switching** (about a quarter second less per switch) and **a faster library audit** on the dashboard (about 40% less time per song).
- **Safer saving.** Saving a song with solos or reference PDFs never leaves the song file missing if something goes wrong partway. StarScore's settings file and the audit cache are written so that a crash while writing can't leave them cut short.
- **The sheet record is left alone if it can't be read.** Before, an unreadable record was replaced, and the Finder colours of sheets outside that export were lost. The export now reports the problem instead.
- **The export progress ends when the export does**, and the score integrity check waits until an export has finished instead of opening a warning over it.

## 1.18.38

- **New part status: "Written, unformatted"** (yellow), between In progress and Needs review: the notes are written but the layout isn't done. It is in every Part status and section Status menu, the part score tab menu, the to-do list and its PDF, and the dashboard. A section or arrangement on Auto shows it when that is its least finished part. In the Finder colours, a sheet folder whose least finished sheet is Written, unformatted is Orange, the same as In progress. Files saved with it open in older StarScore builds as In progress.

## 1.18.37

- **The Keys bass staff always shows in the full score.** A rhythm section keyboard's lower (bass clef) staff is set to "Never hide" in the main score, so it is there to write in even when empty, and stays "Always hide" in the part scores, where it shows only on systems with music. The lead sheet's piano keeps "Always hide" everywhere. Set when a song opens, with Apply default style settings, and on export. Exported sheets and Scores come out as before.

## 1.18.36

- **Additive time signatures keep the song's length.** Adding a time signature one bar at a time re-barred the rest of the stretch at every bar, filling its last bar out with rests (or, in empty bars, cutting it short), so the stretch slowly grew or shrank: on an empty song a pattern stopped early, and deleting it again left a different number of bars. The stretch is now re-barred once, straight into the pattern, with the last bar cut short where the stretch ends. Notes stay where they are and are only tied over the new barlines. Deleting the pattern gives back exactly the bars you had (tested on an empty song and on one full of notes: 32 bars before, after deleting, and after undo; every tie gone again).
- **A selected range limits the additive time signature.** Select two or more bars (a multimeasure rest counts as one) and the pattern fills just those bars, replacing any time signatures in them; the time signature from before comes back after the range. With one bar selected (or one multimeasure rest), the pattern runs from there until the next time signature change or the end, as before. Applying a new pattern over an old one replaces it, and a single number ("3/4") makes an ordinary time signature.
- **Multimeasure rests count composite bars in an additive time signature.** In "4+5/4", six empty bars on a part with multimeasure rests on show as one multimeasure rest of 3 (three bars of 9/4), starting on a composite bar's first bar and holding whole composite bars only. Before, the hidden time signature on every bar of the pattern stopped multimeasure rests from forming at all.
- **Deleting several time signatures at once goes right to left.** Select several time signatures and press Delete once: the last one goes first, so each re-barring only touches bars no later deletion touches again (fewer notes moved and tied). Three patterns deleted together gave back the song's exact 32 bars with no ties.

## 1.18.35

- **Additive time signatures run on.** An additive time signature now always starts at the first selected bar and continues until the next time signature change or the end of the song. Before, a selection of several bars (such as a 20-bar multimeasure rest) limited it to those bars, and the old time signature came back after them.
- **Deleting an additive time signature deletes all of it.** Deleting the visible "4+4+4+3/8" also deletes the hidden time signature on each bar after it, so the bars go back to the time signature before it in one step.

## 1.18.34

- **New scores get a tempo marking in the time signature's beat.** Compound meters (6/8, 9/8, 12/8: a top number above 3 that divides by 3) count the dotted note: 6/8 and 12/8 get "dotted quarter =", 6/4 "dotted half =", 6/16 "dotted eighth =". Simple and odd meters count the bottom note: 4/4, 3/4 and 5/4 get "quarter =", 2/2 and 3/2 "half =", 3/8, 5/8 and 7/8 "eighth =". The number you enter is beats a minute in that note. This applies to New StarScore (its tempo field now says which note it counts in) and to MuseScore's New score wizard, where the tempo note follows the time signature you pick (you can still change it).

## 1.18.33

- **Long instrument names no longer run into the title.** When the instrument name top left would touch the song title (Industrial Park Driveway's "Contrabass Clarinet in B♭"), it is broken over two lines, before "in" when there is one ("Contrabass Clarinet" / "in B♭"), otherwise before the last word, then the last two, until it clears. Mute lines stay under it. This happens on the exported sheets and in the part scores when the song opens.

## 1.18.32

- **Starsign 2.8 house style**: hairpin text (cresc., dim.) in Futura 9 pt instead of Petaluma Script 10 pt. Nothing else changed. Apply default style settings to bring a song's part scores up to it. A different file you saved under the same name in the styles folder is kept beside it.

## 1.18.31

- **One Harmon mute option.** Mutes for this part… lists "Harmon Mute" once; the separate "Harmon Mute (stem out)" option is gone.

## 1.18.30

- **A rhythm part score can hold more than one instrument.** Name a part score "Keys", "Drums", "Percussion", "Guitar" or "Bass" and put two or more of the rhythm section's instruments in it (in the part, tick the eye of another instrument in the Instruments panel). Those instruments are then one player's: Export to Sheets and Demos prints them on one sheet under that name ("GIJO - Keys.pdf" with the Clavinet and the Bass Synth), and their own sheets from earlier exports go to Version History. Marking the part score marks all of its instruments. For the section's status they count as that role: a "Keys" part with no status reads the lead sheet like any keyboard, so G.I. Jorge's untagged Bass Synth no longer makes the Rhythm Section (and every arrangement) Empty. The to-do list, the sheet record and the Piano, Guitar and Bass songbooks treat them the same way.
- **Part scores named by an old scheme are renamed when the song opens.** A lead sheet's part score becomes "Lead Sheet" (Okane's "rad3"), a horn part's takes the part's name (Fish Oil's "ignore6" becomes "6H: Soprano Sax"), and any other part score whose name says nothing about its instruments takes their names in the band's words (Fish Oil's "ignore15", a Rhythm Section keyboard, becomes "Keys", like its exported sheet). Names that do describe the instrument ("Bass (5-String)", "Hammond Organ (1)", "Keys") stay as they are.
- **Pressing M (multimeasure rests on/off) keeps the system locks.** In MuseScore, switching multimeasure rests off deleted every locked system that held a multimeasure rest, so pressing M twice could lose the formatting around rests. Now switching them off keeps those locks. Where switching them on had moved a lock's ends (a multimeasure rest running across the end of a locked system) or swallowed a locked system of rests, switching them off puts the lock back where it was. The song remembers this when it's saved. On G.I. Jorge's 7-Horn Tenor Sax 1 part, pressing M four times keeps all 52 locks and every break.
- **Combine keyboard parts into a Keys part** (StarScore menu and Format menu): the Rhythm Section's keyboard parts go into one part score named "Keys", built on the first keyboard's own part score so its formatting stays. A bass synth is included when the section also has another bass. G.I. Jorge's Clavinet and Bass Synth become one Keys part this way.
- **Mutes for brass parts.** Mutes for this part… (StarScore menu and Format menu) lists the mutes for the part you're viewing: straight, cup, Harmon (stem in or out), plunger, bucket, solotone, pixie, derby (hat) and practice for trumpets and trombones; straight, cup and practice for tuba; straight, stopping and practice for horn. Tick as many as apply, or type in another. The sheets print them under the instrument name top left: "Trumpet in B♭" / "with Harmon Mute" / "and Cup Mute". A Flexible chair can have mutes too; they print only on its sheets for brass instruments, and only the mutes that instrument has (none on its sax, flute, clarinet or string sheets). Copying a part into a new arrangement copies its mutes to the same instrument (the 3-Horn trumpet's Harmon to the 4-Horn trumpet), but not to a different one (the 1-Horn saxophones and trombone made from the trumpet).
- **Mute markings are italic.** Staff text that is just a mute marking ("mute", "(mute)", "open", "(open)", "unmute", "(unmute)", "cup mute", "con sord.", or a mute's name on its own for parts with several, such as "harmon" or "(plunger)") is set in italics when you type it. The same markings are also left off the sheets of instruments without mutes (1-Horn saxophones, Flexible sax and flute sheets), as "mute" and "open" already were. You can switch the italics off again; StarScore doesn't change text that is already there.

## 1.18.29

- **Combined parts follow the arrangements' layouts, section by section.** For each section (from one rehearsal mark to the next), the combined sheet takes its system breaks, "keep together" marks and system locks from the part score of the arrangement that is busiest there (the most notes; the larger arrangement when even). Page breaks become system breaks, since the combined systems are taller and fewer fit on a page. A section whose busiest arrangement has no part score keeps MuseScore's own layout.
- **Copying a layout that has locked systems** no longer sets a system break inside a locked system and takes it out again in the same step. That could crash StarScore when a throwaway copy of the song was closed after its export.
- **A new horn arrangement copied from its neighbour gets all of its formatting again.** Since 1.18.28 the copy is one undo step, and the new part scores were still one long multimeasure rest when the formatting was copied, so some system locks were lost and system breaks stayed at their ends (G.I. Jorge's new 2-Horn from the 3-Horn). The new part scores are now laid out with their music before the formatting is copied.
- **Fermatas on a barline stay on the barline** when copied and pasted, and so in every copy StarScore makes this way (new arrangements copied from a neighbour, combined parts). Before, such a fermata moved onto the note after the barline, or was lost when it sat on the last barline of what was copied.
- **Copy part formatting**: nothing is chosen under "Copy from" until you pick a part, and the part you're viewing is ticked under "Copy to" (and scrolled into view). Both lists are in the band's order: 1H (Trumpet, E♭ Sax, B♭ Sax, Trombone), 2H, 2H Flexible, 3H, 3H Flexible, 4H to 7H (each high to low), Rhythm, the other sections, Big Band, Marching Band, Orchestra, then the scores of several instruments.

## 1.18.28

- **Combined parts.** Export to Sheets and Demos now also makes one sheet per horn chair with that chair's line from every standard arrangement (1-Horn to 7-Horn; not the Flexible ones or the stand-in versions), into the song's "Combined Parts" folder ("CODE - Alto Sax (All Arrangements).pdf"). Chairs go by part name: "Trumpet" (1H–4H) and "Trumpet 1" (5H–7H) are one sheet, "Trumpet 2" another. Where arrangements play exactly the same thing they share one line; where they differ, each different line gets its own voice (the higher line on top: voices 1 and 3 stems up, 2 and 4 stems down), and bold staff text says which arrangements read which line ("3H–5H" above, "6H, 7H" below). As in professional condensing, a grouping holds for a whole phrase (between bars where everyone rests, and between rehearsal marks) rather than changing bar by bar; labels appear only where the grouping changes, and "All" where the lines join again. Bars where everyone rests show one rest. A passage with more than four different lines puts the rest on a second staff that only appears on those systems. Chord symbols, dynamics, hairpins and texts that several lines share are printed once, where MuseScore places them. The sheet takes the look of the chair's own part score. A chair gets a combined sheet when it is in two or more arrangements with notes and one of its sheets is in the export.
- **Playback uses the rounded tempo.** Half-time and double-time sheets and Convert from double time now also play at the rounded number shown ("♩ = 67", not 66.5). The marking can be changed afterwards like any other.
- **Copying horn parts into a new arrangement is one undo step**: the music and every copy's formatting.
- **8/8 bars of slashes get four quarter-note slashes** in the half-time and double-time sheets and in Convert from double time (4/8 and 2/8 count in quarter notes too).
- **Starsign 2.7 house style**: glissando text is off by default. Apply default style settings to bring a song's part scores up to it. A different file you saved under the same name in the styles folder is kept beside it.
- **Doubler recommendation**: when the song's 6- or 7-Horn arrangement has the woodwind doubler on Tenor Sax 2 and you make a 3-, 4- or 5-Horn arrangement, Alto Sax is recommended for the doubler (with two saxes, alto and tenor cover more range than two tenors).
- **New arrangements play with the song's swing straight away.** New instruments used to play straight until StarScore was restarted, because MuseScore only sets each staff's swing when a song opens. The chord-symbol playback of new horn parts now starts muted in the mixer, in the full score and every part score, also before the parts have chord symbols.

## 1.18.27

- **Player annotations.** Band members' own notes on their sheets can now be kept in the song, so a sheet update no longer loses them. In a part score, Add › Player annotations › Add player annotation… puts a note above the selected note or rest and asks whose it is (the roster's player for that sheet is suggested; anyone else can be typed). Other markings (dynamics, fingerings, breath marks, articulations, symbols, fermatas, lines, hairpins, staff text already there) become a player's with Mark as player annotation…; a marking that was also in the full score or other part scores is taken out of those. Unmark player annotation makes them ordinary markings of the sheet again. Annotations are highlighted on screen with the player's name. Export to Sheets and Demos prints every sheet without them (the clean sheet comes out exactly as before) and, for each player with notes on a sheet, a copy with only that player's notes in the song's "Annotated Sheets" folder ("Annotated Sheets/7H …/CODE - Tenor Sax 1 (Ben's notes).pdf"). Since the notes stay in the song, every later export carries them onto the new version. A copy whose player no longer has notes on that sheet goes to Version History.
- **Version History folders list by date.** New folders put the date first ("2026-10-09 Superseded", "2026-10-09 Before standardizing"), so they sort in date order. The existing dated folders in Projects and Sheets and Sheets and Demos were renamed the same way; the old undated V1, V2… folders were left as they were.
- **Exports are faster: the throwaway copies of the song no longer build playback data.** The Scores and version sheets are made from copies of the song; each copy built the notes as the synthesizer would play them, and every edit to a system break or style value rebuilt that for the whole copy: a third of a big song's export. The sheets are identical.
- **Text is measured once.** Measurements of chord symbols, titles, lyrics and other texts are remembered instead of being worked out again at every layout: a quarter of an export's remaining time, and a little off opening, Apply default style settings and every redraw. Together with the change above, Branston Pickle's export went from 254 s to 135 s and Dream of Mushroom's from 561 s to 298 s on the test machine; every sheet, report and saved file came out identical on 15 songs.
- **Apply default style settings is faster.** The song's playback data is rebuilt once at the end instead of once per part score (Last Pint's 45 scores: 107 s to 34 s). The same applies while an export edits the sheet titles.
- **Half-time and double-time exports use less memory and time.** The converted copy lets go of the song's old bars as soon as they are out of the score, and its sheets aren't laid out a second time after printing (Dream of Mushroom's half-time export: 3.8 GB peak before, 3.4 GB now; 369 s to 277 s; sheets identical).
- **Autosave archive: a history file whose index is damaged is kept beside the new one** ("….starhistory.damaged-<date>"), as a damaged frame already was. Before, such a file read as an empty history and the next save wrote a new history over it, losing the versions it held.

## 1.18.26

- **Converted tempo markings are whole numbers.** Half-time and double-time sheets and Convert from double time round each tempo to a whole number ("♩ = 133-153" in half time is "♩ = 67-77", not "66.5-76.5").
- **The horn-copy question only comes up when there is music to copy.** A new horn arrangement next to an empty one is made without asking, and only parts with notes are listed.
- **A damaged autosave history repairs itself.** The versions that still unpack are kept, the history is written again, and the new save goes in; the damaged file is kept beside it ("….starhistory.damaged-<date>"). Before, every later save piled up as a full-size copy that was never packed.
- **The autosave archive window no longer freezes** while it unpacks a version or saves every version as files; it says what it is doing at the bottom.
- **Bars with slashes and rests keep their rests** in the half-time and double-time sheets and in Convert from double time. 1.18.25 turned such a bar into slashes all the way through (G.I. Jorge's "half rest, then four eighth-note slashes" became four quarter-note slashes in half time, so the bar's rests were lost). Now only the slashes are rewritten, one per beat over the beats they cover, and the rests stay.

## 1.18.25

- **Half-time and double-time sheets.** Export to Sheets and Demos has two new ticks for each song: "Export half-time version" and "Export double-time version". Each is remembered for that song until you untick it. A ticked one makes the ticked sheets a second time from a copy of the song with every note half as long (half-time) or twice as long (double-time), the tempo changed to match, into a "Half-Time" or "Double-Time" subfolder of each part folder ("4H/Double-Time/CODE - Trumpet.pdf"). With both ticked, three sets of sheets come out. Their bar numbers match the standard sheets: in double time both halves of a bar carry its number; in half time a bar made of two carries the first one's number and the numbering skips the second. These sheets have no version numbers or changelog of their own; a new export replaces them. The song itself is never changed.
- **Bars of slashes keep one slash per beat** in the half-time and double-time sheets and in Convert from double time: as many slashes as the time signature's top number (4/4: four quarter-note slashes, never two half notes or eight eighths); 2/2 also gets four quarter-note slashes.
- **Convert from double time** now changes every number in a tempo marking ("♩ = 133–153"), not only the first.
- **New horn arrangement: copy the parts from the neighbouring one.** Adding an arrangement (or section) with one horn more or fewer than one the song has, StarScore asks whether to copy the parts on the same instruments: 5H Trumpet 1 into 6H Trumpet 1, 5H Trumpet 2 into 6H Trumpet 2, 5H Alto Sax into 6H Alto Sax, 5H Tenor Sax into 6H Tenor Sax 1, 5H Trombone into 6H Trombone. Each copy gets the music and its part score's formatting (style, system and page breaks, bar widths) and text positions, and is marked Sketch. A part with no partner stays empty.
- **Copied parts keep the source part's formatting and text alignment.** Wherever StarScore makes a part from another (the 1-Horn Trumpet's Eb Sax, Bb Sax and Trombone sheets, the main low horn's stand-in versions, Flexible chairs, the new horn copy), the new part score now gets, besides the breaks, bar widths and style: each text's alignment, font and frame as well as its position; chord symbol positions (matched by place, since a transposing instrument's chords read differently); codas, segnos and jumps; the title frame's height and where its texts sit; and hairpins and other lines. A text's alignment or font set this way stays in that part score only (it used to be able to pass on to the main score and the other parts).
- **Alternate chord symbols are 11 pt** in the Starsign house style (were 10). Apply default style settings to bring a song's part scores up to it.
- **Autosave archive.** Each time a song is saved, the version being replaced is kept (at most once every 10 minutes, and only when it changed). File › Autosave archive… lists every kept version of each song; "Open this version" opens it as its own file (the song itself is not changed), and "Save all versions as files" unpacks them all. Versions are packed into one history file per song in "StarScore Studio/Autosave Archive" on this Mac, each stored as only what changed since the one before, so a 10-minute change costs kilobytes instead of a whole 4 MB file (five real G.I. Jorge saves: 15.0 MB as files, 4.7 MB packed). Older versions are thinned: every version from the last 2 hours, then one per hour for the last day, one per day for the last 30 days, one per week for the last 6 months, and one per month after that, kept for good.
- **Progress colors.** Select measures on one or more staves and mark them finished (green), needs review (orange) or unfinished (red): from the new Progress row in the StarScore panel, from View › Progress colors, or by right-clicking the selection › Progress color. "Show progress colors" hides or shows all of them at once; hiding them keeps them. They are drawn on screen only, so they never appear in PDFs, prints or exported files. They are saved with the song, stay with their measures if measures are added or deleted elsewhere, show in part scores too, and can be undone. Keyboard shortcuts can be given to the four marks in Preferences › Shortcuts.
- **Diminished-major 7 footnote.** A sheet with the diminished-major 7 diamond now explains it at the bottom of the first page where it appears ("The ◇ symbol denotes a diminished-major 7 chord."), like the minor-major 7 note. When both symbols first appear on the same page, one short note covers both ("△ = minor-major 7; ◇ = diminished-major 7.").
- **Apply default style settings** is at the bottom of the Format menu, with Load style and Save style.
- **Crash fix: StarScore crashed on every start** after its saved panel layout was damaged (7 Oct). A panel that can't go back where it was is now put beside the score.
- **Crash reports are copied to "StarScore Studio/Logs/Crash Reports"** each time StarScore starts (the newest 20), next to the logs, so they can be read from there.
- **Standardize this song** keeps showing the sections that were showing before, instead of switching to the first arrangement.

## 1.18.24

- **Standardize this song** (Format menu). Gives an older song the standard layout: the "2-Horn / 3-Horn Any (all keys)" and "Flexible (all keys)" sections become the standard 2-Horn and 3-Horn Flexible sections (each chair gets the music, part score layout and status of its old "in C" part, or the "in Bass Clef" part for a trombone chair); "Strings & Extras" becomes a String Duo, Trio, Quartet or Quintet section and its other instruments are removed; "Horn Section (full score)", "Player Lead Sheets", "Full Score" and other arrangements not made from a template are removed; a 3-Horn Flexible section's old hidden "Horn 1 (Flute)" chair goes; part scores of several instruments that aren't an arrangement's score, empty ones and second part scores of the same instrument are removed. It lists every change first, and copies the file as it was to Version History/Before standardizing (date). All songs in Projects and Sheets were standardized this way on 7 October 2026 (originals in each song's Version History/Before standardizing 2026-10-06).
- **Part scores named after players** ("Cory", "Harley", "Katelyn") are renamed after what's in them ("Drums Lead", "Bass (5-String)", "Cello") when a song opens.
- **"Part styles" is now "Apply default style settings"** (Format menu, the dialog, the status bar menu and the dashboard button "Apply default style settings to every song").
- **Exporting to Sheets and Demos takes about half the time** (measured here, every sheet identical: Branston Pickle 11 min 30 s → 3 min 40 s, Bet 3 min 5 s → 1 min 40 s, Balkan Wedding 5 min 45 s → 3 min 45 s, The Courier 8 min 10 s → 6 min 10 s, Last Pint 13 min 10 s → 8 min 30 s). The song's working copies are written straight from the open song instead of saved, loaded and saved twice more (44 s → 6 s); a Flexible version sheet no longer loads a copy for nothing or lays out the copy's whole score after every change (10 s → 1.5 s a sheet); and working out a Score's systems and tightening its wrapped bars lay out only the systems concerned after each try, not the whole Score.
- **Saving is faster** (Branston Pickle here: 22 s → 14 s; with 24 part score tabs open 33 s → 14 s; the file is the same). To write a score that has multimeasure rests and hidden parts, MuseScore shows every part, lays the score out, writes it and puts the parts back; that put-back laid out every open score again, for the main score and for each Section Score. Now only the score being written is laid out again.
- **Setting the version number is far faster** with part score tabs open. Every export sets the version first; the footer of each score was changed with every open score laid out again each time (Branston Pickle with 52 part scores open: 9 min 20 s before any sheet was made; now 35 s). The same wait was in "Update all sheets", which sets the version several times a song.
- **Applying the default style settings is much faster** on songs with many part scores open (Branston Pickle with its 52 part scores open, measured here: 20 min 20 s → 1 min 30 s). Each score was laid out again, along with every open score, after each of the three steps of its restyle; now each score is laid out on its own, and only where the house style measures it.

## 1.18.23

- **Convert from double time** (Format menu). Rewrites a song written in double time in standard time: every note, rest and marking half as long, the tempo halved (♩ = 216 becomes ♩ = 108), so it sounds the same. In 4/4 two bars become one, counted within each stretch between rehearsal marks, repeats and double barlines; a bar left over keeps its length (G.I. Jorge's fermata bar). In 7/8 each bar becomes a bar of 7/16 (Shatter). Bar repeat signs are written out; rehearsal marks, system texts, repeats, barlines, ties, rit. lines and fermatas come through. Hand-placed system breaks in the parts are dropped. One step: Undo puts it all back.
- **Crash fix: opening songs one after another.** A song closed before the audio engine had answered its player's first requests could crash StarScore (seen while opening songs in a row and while making audio demos).
- **Paste half/double duration** pastes any number of whole bars, and no longer crashes on markings placed between notes.

## 1.18.22

- **Section Scores in their own subfolder.** Each part folder (4H…, Big Band, the String sections…) keeps its Scores in "Section Scores". The Flexible 2H and 3H folders have a subfolder per horn ("Horn 1", "Horn 2"…) plus "Section Scores". Sheets already exported are moved there the next time the song is exported or "Update all sheets" reaches it: same files, same versions, nothing archived. The sheet record and the folder colours follow.
- **Song files get the standard name.** When StarScore starts, a song's file named just "Title.starscore" ("Another One.starscore") is renamed "CODE - Title.starscore" ("ANON - Another One.starscore"), using the code from Sheets and Demos. A song that's open is left alone, files with any other name are never touched, and the recent files list follows the new name.
- **Apply part styles to every song** (Home › Dashboard): opens each song in turn, applies Format › Part Styles › Apply to open score now, and saves it. The song open when it starts is saved first.

## 1.18.21

- **Faster exports.** A Score's working copy is no longer laid out again after every change while it's prepared, only where something has to be measured, and a part sheet isn't laid out a second time after it's printed. About a third less time on Bet and The Courier here; the sheets come out the same.
- **Evenly spaced version numbers.** In the footer, the digits and dots of "Version 5.1.0" are now the same distance apart (the font set "5." tight and left a wide gap after the "1").
- **"Save first?" only when there's something to save.** Exporting right after saving no longer asks.

## 1.18.20

- **Minor-major 7 footnote** now reads "The [symbol] symbol denotes a minor-major 7 chord." (was "indicates"). "Update all sheets" makes again only the sheets that have the footnote, each with its version's last number raised.

## 1.18.19

- **Scores renamed** to CODE - Section Score (Concert), Section Score (Transposing), Section Score (Bb), Section Score (Eb), Section Score (Treble Clef) and Section Score (Bass Clef), in every section folder (the Flexible ones have Concert, Bb, Eb and Bass Clef; the String sections have Concert). The page still says "Concert Score", "B♭ Score"… top left. Scores already exported under 1.18.18's names ("CODE - Concert Score.pdf"…) are renamed in place the next time the song is exported or "Update all sheets" reaches it: same file, same version, nothing archived.
- **The keyboard player's sheet is always "Keys".** Any rhythm-section instrument that isn't guitar, bass, drums or percussion (accordion, vibraphone…) now exports as CODE - Keys (Last Pint's came out as "PINT - Accordion"). A sheet under the old name goes to Version History once the Keys sheet is written. Tambourine and cowbell count as Percussion.

## 1.18.18

- **Six Scores for every section.** Each 2- to 7-Horn, Big Band, Orchestra and Marching Band folder now gets CODE - Concert Score, Transposing Score, Bb Score (every instrument written in its nearest B♭ transposition, all in treble clef), Eb Score (the same for E♭), Treble Clef Score and Bass Clef Score (every instrument in that clef, at the octave that suits its notes). Drums, percussion and two-staff instruments (piano, marimba) stay as they are. The Flexible Scores are renamed to match: Concert Score, Bb Score, Eb Score, Bass Clef Score. Scores under the old names ("CODE - Score.pdf", "CODE - Score (Transposing).pdf"…) go to Version History once the new ones are written. A folder's six Scores share their systems. The String sections' Score is now "Concert Score".
- **Scores have their name top left** ("Concert Score", "B♭ Score"…) and **multimeasure rests**.
- **Minor-major 7 footnote.** A sheet that uses the minor-major 7 symbol says, in the left footer of the first page where it appears: "The [symbol] symbol indicates a minor-major 7 chord.", in the symbol the sheet uses. Only on exported sheets; the score in StarScore doesn't show it.
- **Export progress.** The Export to Sheets and Demos window shows what's being made while it exports: sheet (or demo) N of M, a progress bar and the file name. The window stays responsive; clicks wait until the export is done.
- **Update all sheets** (Home › Dashboard). After a StarScore update that changes how sheets are made, it opens each song whose sheets were made before that change, one at a time, and makes again every sheet that was exported while Finished and is still Finished. Only the sheets that come out differently are written: the old file goes to Version History and the version on the sheet goes up by its last number (1.2.0 becomes 1.2.1). Sheets that aren't Finished are never touched, and each song is saved afterwards. The window shows the song, the sheet being made and a line per song when it's done; it can stop after the current song. Each song's sheet record now keeps the StarScore version and sheet format it was exported with.
- **The diminished circle matches the ø.** It's now the chord font's own small o, raised and sized like the half-diminished ø (Last Pint's A°/C♯ sat too high and too far from the A).
- **Scores export faster.** A folder's six Scores share one prepared copy of the song, and the tighter-spacing pass tries every run of bars in one go.

## 1.18.17

- **Windows build fixed.** 1.18.16's new crash report used a Mac and Linux function, so the Windows version didn't build. Mac and Linux are unchanged.

## 1.18.16

- **"Flexible horns" menu in the status bar**, next to Concert pitch, shown when the score has a Flexible section. It sets how the Flexible 2- and 3-Horn chairs look while you write, in every score, and is remembered on this computer: Reference instruments (B♭ Trumpet, (Alto Sax), Tenor Sax; the default), Range-optimized clefs (treble, soprano, alto), Standard clefs (treble, bass), Treble clefs (the bottom horn in treble clef an octave down), Bass clefs (the upper horns in bass clef an octave up), B♭ instruments (B♭ Trumpet, Tenor Sax) and E♭ instruments (Alto Sax or E♭ Trumpet, whichever keeps more notes in range, and Bari Sax). The chairs' ranges and the exported sheets don't change. It replaces the section menu's "Show the chairs".
- **No Parts window.** It's gone from the File menu and the toolbar; StarScore's sections show, name and make the parts. The part tabs have no close button (and middle-click doesn't close them); the tab's right-click menu still can.
- **Showing a section opens its part scores.** The arrangements' own scores ("3-Horn Standard Score") and the full horn scores kept from the converted files ("2-Horn Arrangement") stay closed, unless "Section scores visible" is on (StarScore menu, ⋯). The percussion part (Congas…) stays closed unless "Percussion score visible" is on. Both switches are remembered on this computer.
- **A piccolo section gets its Flute part right away.** A new 3- to 7-Horn section with the doubler on piccolo now has its Flute part from the start, empty, so music can be pasted into both. Marking the Piccolo Finished while the Flute is still empty fills it from the Piccolo as before; a Flute with music in it is left alone.
- **String sections**: String Duo (violin, cello), String Trio (violin, viola, cello), String Quartet (violin, viola, cello, double bass) and String Quintet (two violins, viola, cello, double bass), in Add section. They belong to no arrangement; each exports to a folder of its own ("String Trio") with a sheet per instrument and a Score.
- **"Woodwind doubler plays"** lists just the instruments ("Piccolo", not "Ben on Piccolo").
- **Scores: bars that spill onto an extra system are spaced tighter.** When the bars between two system breaks take two or three systems and a little tighter spacing saves one (Bumper Cars' 4-Horn Score: 4 bars, then 1 alone), the Score's bars are tightened like the "{" shortcut, at most down to 0.3. MuseScore never spaces notes closer than its minimum, so they stay readable; where it doesn't save a system, nothing changes. Bumper Cars' 4-Horn Score is now 4 pages instead of 5.
- **Crash reports.** A crash now writes "crash-<date>.txt" with the call stack to ~/StarScore Studio/Logs, and macOS writes its own crash report as well (StarScore used to quit without one). The log also says which sheet or audio demo was being made, so a crash during an export can be traced.
- **Audio demos: the arrangement's score is laid out first.** Bet crashed while making its audio demos; the arrangement scores they're made from are usually closed and had never been laid out. Fixed for that cause; if it happens again, the new crash report will say where.
- **Crash fixed: closing a part score that had no tab** (its section switched off).

## 1.18.15

- **4- to 7-Horn trombones in tenor clef too.** Every Trombone sheet in a 4-, 5-, 6- or 7-Horn arrangement is now also exported as "CODE - Trombone (Tenor Clef).pdf" next to the bass-clef sheet, like the 1-Horn and Flexible trombones. The Bass Trombone stays in bass clef only. Big Band, Orchestra and Marching Band sheets don't change.
- **No mute or open markings on the 1-Horn saxophones.** When the E♭ and B♭ Saxophone sheets are made from the Trumpet, its mute and open markings are hidden on them, and the 1-Horn saxophone sheets made before this are cleaned at export. The 1-Horn Trombone keeps them.

## 1.18.14

- **Finished songs leave the Home to-do list.** For songs converted from older files (The Courier, Amplitudes and the rest of the audit list), an arrangement now counts as done once it's Finished by the same rule as everywhere else. Before, every sheet in it had to be Finished itself, including a Keys or percussion part tagged Empty, which only reads the lead sheet. So The Courier stayed on the list with every arrangement Finished. Sheets audited one by one still count as before.
- **1- to 7-Horn sections always show all their chairs.** Turning a horn section on in the View panel now brings back every instrument in it; only stand-in versions are remembered as shown or hidden. An instrument shown by the Audit panel for an issue and still showing after the file was reopened could be remembered as the section's only instrument. That's how Amplitudes' 2-Horn ended up showing just the Tenor Sax (its 3-Horn just the Trumpet, its 7-Horn just the Tenor Sax).
- **Hidden horn chairs count toward the status.** A 1- to 7-Horn section's status now counts every chair, hidden or not, since each one is exported. Amplitudes' 2-Horn Section read Finished from its Tenor Sax alone; its Trumpet isn't marked yet. G.I. Jorge, Shatter, Shofukan (Baritone Sax) and Meevin (Tenor Sax) had a hidden 7-Horn chair too.

## 1.18.13

- **The 1-Horn sheets made from the Trumpet start as Needs review** (the E♭ Saxophone, B♭ Saxophone and Trombone), instead of Finished like the Trumpet.
- **A new 3- to 7-Horn section or arrangement suggests the doubler's instrument from the song.** If the song already has a 3- to 7-Horn section where the woodwind doubler plays something other than his usual chair (flute, clarinet, bass clarinet, piccolo…), that instrument is the recommended one and is picked to start with. Otherwise the defaults stay: alto sax in 3–5-Horn, soprano sax in 6–7-Horn.
- **Bar numbers on Scores no longer touch the bracket's top hook.** After the Score is laid out, StarScore measures each bar number at the start of a system against the hook and raises the bar numbers until there's a gap. Checked on every section Score of The Courier, Bumper Cars and Top Hat, and on test Big Band, Orchestra and Marching Band scores.
- **Big scores fit their pages.** When a Score's systems are taller than the page (a full orchestra or marching band), StarScore shrinks the staff size step by step until every system fits, instead of leaving page 1 blank and running systems off the bottom.
- **No leftover brackets on Scores.** Old brackets in the song's third bracket column stayed next to the new ones: a second brace on the Big Band piano and the Marching Band marimba, and a thin extra bracket over Horns 1–2 on the 3-Horn Flexible Scores (Bumper Cars, Top Hat).
- **Export crash fixed.** Closing or pressing Escape on the Export Sheets and Demos window while it was still exporting (audio demos take a while) crashed StarScore. The window now stays open and can't be closed until the export is done.

## 1.18.12

- **1-Horn starts with the Trumpet only.** A new 1-Horn arrangement has just the Trumpet sheet. When the Trumpet is marked Finished, StarScore makes the other three sheets from it without asking: the same music, the part score's style, system and page breaks, system locks, bar widths and text positions (hidden texts stay hidden), marked Finished like the Trumpet. The B♭ Saxophone and the Trombone are an octave lower; the E♭ Saxophone plays the Trumpet's pitches. They're made again if one is deleted and the Trumpet is marked Finished again. Changing the Trumpet afterwards doesn't change them.
- **1-Horn saxophones renamed.** The 1-Horn Alto Sax and Tenor Sax are now "E♭ Saxophone" and "B♭ Saxophone": "1H: Eb Saxophone" in the score, "CODE - Eb Saxophone.pdf" in Sheets and Demos. Sheets exported under the old names are moved to Version History at the next export.

## 1.18.11

- **1-Horn Trombone in tenor clef too.** The 1-Horn export now also writes "1H/CODE - Trombone (Tenor Clef).pdf" next to the bass-clef Trombone sheet, made from the same part, like the Flexible trombone sheets.

## 1.18.10

- **Scores join systems that fit together.** When a section Score takes its systems from the part with the most system breaks, StarScore then tries taking out each system break. The break stays out only when both systems fit whole on one system: a 3-bar and a 2-bar system become one of 5, two 4-bar systems one of 8 (then it tries adding the next system), but never 4 + 1, 5 + 3, 6 + 2 or 7 + 1. A system that starts with a rehearsal mark is never joined to the one before it. The result is kept in the song and used again at the next export, until the reference part's systems, the song's bars or its rehearsal marks change.
- **The Rhythm folder's colour comes from drums, bass, guitar and the lead sheet.** Once those are exported and Finished, `1 Rhythm` is green; the Keys sheet (and percussion) no longer count. The Keys sheet no longer counts toward the song's colour either.

## 1.18.9

- **Keys, drums or percussion tagged Empty no longer make every arrangement grey.** In a Rhythm Section, a part tagged Empty now counts like one with no tag: its player reads the lead sheet. The Courier's Keys part was tagged Empty, which made its Rhythm Section Empty, and every arrangement includes the Rhythm Section.
- **Warning right after an edit that damages the score.** A moment after any edit, StarScore checks that every bar has the right number of beats. If one doesn't, it says which bar, so you can undo the edit straight away. The Courier's bar 60 had an extra half rest on top of a whole note; saved like that, the file only opens as a damaged score.
- **Repair when opening a damaged score.** Opening a damaged score ("Open anyway") offers to remove rests that sit on top of notes in the same voice, the damage The Courier had. The notes stay as they are.
- **No more saving a damaged score without being asked.** Saving while a solo transcription was open saved the main score without MuseScore's "this score is corrupted" question. A damaged main score is no longer saved from there.
- **A copy of the log in StarScore Studio/Logs.** On a computer with a "StarScore Studio" folder in the home folder, the app also writes its log there (the last 14 days are kept), so Claude can read it to find what damaged a score.

## 1.18.8

- **Export Audio Demos.** Export to Sheets and Demos has a new "Export Audio Demos" checkbox (off each time the window opens). When it's ticked, the export also writes a 48 kHz, 24-bit WAV of each 2- to 7-Horn Standard, Big Band, Orchestra and Marching Band arrangement the song has, into the song's Demos folder: "CODE - 3-Horn Arrangement Demo.wav" and so on. Flexible arrangements get no demo.
  - Each demo plays the arrangement's own score with the Lead Sheet muted, the rhythm section and the arrangement's horns playing, and every chord-symbol track muted except on keys and guitar.
  - Stand-in versions stay muted: in 7-Horn only the main bass horn plays, and with a piccolo the piccolo plays and its flute version is muted.
  - A demo with the same name is replaced and the old one deleted (not archived).
  - With no sheets ticked, the button exports just the demos.
- **No second bass clarinet in 7-Horn.** When the woodwind doubler plays Bass Clarinet, Bass Clarinet is no longer offered as the 7th horn, and "Make the other versions…" doesn't make a Bass Clarinet version of the 7th horn. A Bass Clarinet version a song already has is left alone; delete the part to remove it.

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
