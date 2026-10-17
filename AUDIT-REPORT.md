# StarScore Studio code audit, 17 Oct 2026

Branch `audit-2026-10-16`, based on 1.18.53 (d8c7e5e0). Six commits. Every changed file was compiled with
`-fsyntax-only` using the main build's flags (unity-build include paths swapped to the worktree). Nothing was
built or run; the worktree has no build directory. Joel's files were not touched or read.

What I read: all of starscorebandexport.cpp, starscoreservice.cpp (+ .h), starscoreautoarchive.cpp,
starscoredemos.cpp, starscoreupdate.cpp, starscorelayoutcheck.cpp, starscorehouse.cpp, starscoreengraving.cpp,
starscoredoubletime.cpp, starscorehorncopy.cpp, starscorehornkeys.h, the organizer's orgfiling.cpp / orgcore.cpp /
orgstores.cpp, the sheet record writer, bandexportmodel.cpp, dashboardmodel.cpp, songbookmodel.cpp (parts),
starscoreimport.cpp (tidyOpenedScore, mergeBassParts), the StarScore diffs against v4.7.5 in
src/engraving/rendering and src/engraving/editing, starscore_macos_install.sh, starscoreupdatelock.h and
appupdatescenario.cpp. The rest (audit, chord charts, combined parts, compare, todo, organizer HTML) was scanned
for unchecked container access, timers outliving their objects, unbalanced prepare/commit pairs and file deletes.

## Changes committed

### 1. fb89cdcc — Autosave archive: never lose a waiting copy
File: src/project/internal/starscore/starscoreautoarchive.cpp (packSong, readSong).

Wrong: `packSong` only took a waiting copy into the history when its stamp was newer than the newest version
already there, but afterwards deleted *every* waiting copy. A copy with an older stamp was deleted unarchived.
The stamps are local times with no zone (`yyyy-MM-dd HHmm`), so on 1 Nov 2026, when the clocks go back, every
autosave copy made in the repeated hour would read "older" than the version before it and be lost. (This was the
open item "autoarchive can delete a waiting autosave older than the newest Version History entry" from the
1.18.39 audit.)

Changed: all waiting copies go into the item list and the list is stable-sorted by time. The existing loop
already copes with a copy between two old versions (the later old version is patched afresh against the new
neighbour instead of reusing its stored patch; `prevOldRaw` only tracks old versions). On the common path (every
copy newer than the history) the order is exactly what it was.

Also: `readSong` now fails on a zip entry that reads back empty while the directory says it has a size
(`ZipReader::fileData` returns an empty array without setting the error), so a half-read copy isn't stored as a
"version". A copy that can't be read is set aside as `<copy>.unreadable` instead of being deleted.

Checked: syntax compile; reasoning above. The pack already verifies the written history reads back before
deleting the copies, which still guards the write. Risk: low (background packer only; the song files are untouched).

### 2. edcf7afc — injectSolos: leave the saved song alone when an entry can't be read back
File: src/project/internal/starscore/starscoreservice.cpp (injectSolos).

Wrong: after every Save the .starscore is rewritten to add the solo and reference files. Each original zip entry
is copied with `reader.fileData(name)`; that returns an empty array, without setting the reader's error, when an
entry can't be unpacked. The rewrite would then have replaced the just-saved file with one whose main `.mscx`
entry is empty. Unlikely (the file was written by MuseScore a moment before) but the file lives on Google Drive.

Changed: an entry that reads back empty while its size isn't zero stops the rewrite; the temporary file is
removed and the saved file stays exactly as MuseScore wrote it (references and solos stay in their working copies
and go in on the next save). Logged as an error.

Checked: syntax compile; `ZipWriter::close()` is called before removing the temp so the handle is closed on
Windows too. Risk: low.

### 3. 7afbbe5e — Audio demos: the old demo goes only once the new one is in place
File: src/project/internal/starscore/starscoredemos.cpp.

Wrong: the old `.wav` was `QFile::remove`d and then the new one renamed over it; a rename Drive refused left the
song with no demo at all (open item "demos remove-then-rename" from the 1.18.39 audit).

Changed: old demo → `.<name>.old`, new → name, old removed last; on a failed rename the old one is put back. The
demo is still deleted, not archived, as Joel asked on 5 Oct.

Checked: syntax compile. Risk: low.

### 4. e89b3cad — Layout checks: compare texts page by page
File: src/project/internal/starscore/starscorelayoutcheck.cpp.

Performance: `layoutIssues` and both pair scans in `separateOverlappingTexts` compared every pair of texts on the
sheet (chord symbols and bar numbers included) and skipped pairs on different pages. `placedTexts` lists texts
page by page, so each page is now scanned on its own. Same pairs, same order (pages are contiguous and in order
in the list), so the output is identical; the comparisons fall from n² to Σ(n_page²). These checks run up to six
times per printed sheet, so this matters most on the section Scores of G.I. Jorge and Branston Pickle.

Checked: syntax compile; the order argument above. The `!movable(a)` test moved out of the inner loop (same
result). Risk: low.

### 5. dff02fc5 — Never write StarScore data back over a tag that couldn't be read
Files: starscoreservice.cpp (fromJson, storeTo, ensureFileId), starscoreservice.h (Data::unreadable).

Wrong: `fromJson` returns empty `Data` when the `starscore` meta tag holds something that isn't JSON. `ensureFileId`
runs on every file open and, finding no fileId in that empty data, wrote the tag back with just a new id. Any
`store()` did the same. One damaged tag plus one Save would have wiped the song's sections, arrangements,
references, export signatures and system cache. I know of no way the tag gets damaged in normal use (it is
written by QJsonDocument and round-trips through the mscx), so this is a safety net, not a fix for a seen bug.

Changed: `Data::unreadable` is set by fromJson on a parse failure; `storeTo` refuses to store such data (error in
the log) and `ensureFileId` skips it. Readable files: no change at all.

Checked: syntax compile of starscoreservice.cpp, starscoreaudit.cpp, starscoreimport.cpp, starscoretodo.cpp
(users of `Data`). Risk: low.

### 6. 0b271adf — Copy part formatting: Standard horn groups from starscorehornkeys.h
File: src/project/qml/MuseScore/Project/internal/StarScore/copylayoutmodel.cpp.

Refactor (item 3 of the brief): the grouping of part scores parsed section keys with its own
`^([1-7])-horn(-any)?$` and a separate `4-horn-2tpt` branch. It now asks `hornSectionSize()` for the Standard
sections (range checked 1–7 so the result is identical) and keeps a regex only for the Flexible keys, which the
header has no helper for. Same groups for every existing key.

Checked: syntax compile; diff reviewed line by line. Risk: low.

Suggested changelog lines for the merge (newest first):
- Autosave archive never deletes a copy it hasn't archived (the hour the clocks go back would have lost them).
- Saving never rewrites the song file with an entry that couldn't be read back; a damaged StarScore data tag is
  left in the file rather than overwritten.
- Audio demos: the old demo is removed only after the new one is in place.
- Export: the pre-print layout checks compare texts page by page (faster on big Scores).

## Problems found but not changed (most serious first)

1. **macOS install, SIGKILL between the two `mv`s** — starscore_macos_install.sh:208–219. If the script is killed
   with SIGKILL (not catchable; e.g. a forced shutdown) after `mv "$APP" "$OLD_APP"` and before `mv "$STAGING"
   "$APP"`, /Applications has no visible StarScore: the old one sits as `.StarScore Studio old <pid>.app` and the
   new one as `.StarScore Studio update <pid>.app`. HUP/INT/TERM are handled (the EXIT trap restores OLD_APP).
   Suggested fix: none in bash is atomic for directories; the window is a few milliseconds. Document the recovery
   in the changelog/notes: in Terminal, `mv "/Applications/.StarScore Studio old "*.app "/Applications/StarScore
   Studio.app"` (or the update one). Optionally the lock check in starscoreupdatelock.h could, on start, look for
   such a leftover when running from a bundle whose name is the hidden one — but the app isn't running then, so
   it can't help itself.

2. **Export bumps the version before it knows the export worked** — bandexportmodel.cpp:366–379. `setScoreVersion`
   stores the new version in the song (and relays out every open score) before `exportToBandFolder`; if the export
   then fails, the song keeps the raised version with no sheets printed at it. Suggested fix: on `!summary.ret`,
   `setScoreVersion(previous)` (the footer needs the version set before printing, so the order itself must stay).

3. **starscoredoubletime.cpp:406 relies on `&&` binding tighter than `||`** —
   `sp->isTie() || sp->isSlur() && !crossesEdge(...)`. It does what the comment says (ties skipped; slurs skipped
   unless they cross a chunk edge), but a reader, and -Wparentheses, will trip on it. Suggested fix: parentheses
   around the `&&` term.

4. **Cloned annotations leak when they find no bar** — starscoredoubletime.cpp:264–268 clones every rehearsal
   mark, system text and tempo marking before the old bars go; at 590–603 a clone whose target bar or segment is
   missing is skipped and never deleted (its parent pointer is also stale, but never dereferenced). Suggested
   fix: `delete a.clone` on those two `continue` paths.

5. **Standardize and detectSections don't create the 4-Horn 2-Trumpet arrangement for an orphan section** —
   starscorestandardize.cpp:471 (`^([1-7])-horn(-any)?$`) and starscoreservice.cpp:3216 (`^([2-9])-horn(-any)?$`)
   map a section key to its arrangement key by hand, so a song with a `4-horn-2tpt` section whose arrangement was
   deleted gets no arrangement back. Only reachable by deleting the arrangement by hand. Suggested fix: add
   `standardArrangementOfSection(sectionKey)` to starscorehornkeys.h ("3-horn" → "3-horn-standard", TWO_TRUMPET_4H
   → itself) and use it in both places. Not done: it changes behaviour, and I couldn't test it.

6. **offerMatchingHornParts never offers a 4-Horn 2-Trumpet section as the neighbour** — starscorehorncopy.cpp:56
   (`hornCount` is `^([1-9])-horn$`), 215–223. A new 3-Horn or 5-Horn is offered copies from the 2H/4H or 4H/6H
   sections but not from a 4-horn-2tpt. Probably intentional (the 2-Trumpet is built from the 3H and 5H), so just
   noting it. If wanted: `hornSectionSize` instead of `hornCount` in the neighbour loop.

7. **Half of the horn-key logic still lives in hand regexes** — besides 5 and 6: starscoreaudit.cpp:430 and
   :1960 (`auditArrangementColumn` handles 4-horn-2tpt explicitly, fine), starscorecombined.cpp:222
   (`^([1-7])-horn$`: the Combined Parts deliberately leave the 2-Trumpet out), starscorebandexport.cpp:868, 1878,
   4563 (Flexible `-horn-any` only; no helper exists). None is wrong today. A `flexibleSectionSize(key)` helper in
   starscorehornkeys.h would let the three bandexport regexes and copylayoutmodel's go.

8. **`writeVersion` loads the chairs copy once per version sheet** — starscorebandexport.cpp:3325–3367. A 3-Horn
   Flexible section has 3 chairs × 5–6 seats = up to 18 loads (the log's "load the chairs copy" lap). Reusing one
   load per chair would need the instrument rewrite undone between seats; the code says a copy edited for one
   sheet isn't reused on purpose. Leave unless the lap times say it's the biggest cost.

9. **planBandExport reads codes.json from Drive on every call** — starscorebandexport.cpp:680; called by
   `labelPartBooks()` from setPartMutes, togglePartFlugelhorn, createSection, applyStyles, copyHornParts,
   changePartInstrument, tidyOpenedScore. One small file read per UI action; a cache keyed on path + mtime would
   remove it. Low value.

10. **`fitInstrumentName` / `fitArrangementLabel` drop inline formatting** — starscorehouse.cpp:696 writes
    `(variants[i] + rest).toHtmlEscaped()`, so a bold or font tag in a hand-formatted instrument name is lost when
    the name is broken over two lines. Cosmetic; only for names that overlap the title.

11. **Roster::save drops unknown keys** — organizer/orgstores.cpp:82–98 writes `version/about/players/
    fileNameHints` only; anything added to roster.json by hand is lost the next time the roster dialog saves.
    Only reachable from the dialog.

12. **loadFrom cache keyed on the MasterScore pointer** — starscoreservice.cpp:751, 870–873. A freed score's
    address can be reused by a new one; the tag + parts fingerprint comparison makes a stale hit return the same
    data it would compute anyway, and the cache is cleared past 8 entries. Harmless; noting it so nobody keys
    anything else on the pointer.

13. **Known and still open from the 1.18.39 audit**: `removeArrangement`'s metaTag change isn't undoable;
    `setPartName` in standardizeHornNames/finishNewParts isn't undoable; a renamed song orphans its autosave history
    (the history is keyed on the file's base name, so "CODE - Title" vs "Title" start separate histories).

Nothing else of substance: prepare/commit pairs are balanced (checked by script, the only hits were returns inside
lambdas); every QTimer lambda in StarScore's own code has a QObject context (`m_timerGuard`, `this`) or captures
shared_ptrs (`qApp` timers); the one bare `[this]` timer, projectactionscontroller.cpp:583, is a 0 ms timer on a
singleton that lives as long as the app;
all QML models that subscribe to notifications are Asyncable; file moves in the export and organizer go through
rename-only `moveItem`/`QFile::rename` with QSaveFile for new files, and nothing in the song folders is deleted
except the demos (Joel's choice) and emptied folders via `rmdir`. The update lock (starscoreupdatelock.h) is
believed only with a fresh two-number lock, a live pid and "starscore-update-" in its command line; every
unexpected state deletes the lock, so it can't keep StarScore from opening.

## What to test after merging

Build and install the dmg, then:

1. **Autosave archive** (change 1). Open any song (Bet is quick), save, wait 10 minutes, change a note, save again.
   `~/StarScore Studio/Autosave Archive/Incoming` should empty within a minute and `<song>.starhistory` should
   grow; File › Autosave archive… should list both versions and open the older one. On the Linux test build also
   run the autotest steps `autoarchivetest` and `archiverepairtest:DIR` (they exercise packSong directly).
2. **Save with references** (change 2). Open a song that has Reference PDFs (and a solo if any), save, close,
   reopen: the Reference PDF panel must still list and show them. Check `~/StarScore Studio/Logs` has no
   "could not read … back from" line.
3. **Audio demos** (change 3). Export G.I. Jorge with "Audio demos" ticked. Demos/ should hold one
   `GIJO - N-Horn Arrangement Demo.wav` per Standard arrangement, each replaced (new date), with no leftover
   `.…Demo.wav.old` or `.part` files.
4. **Export output unchanged** (change 4). Export Bet and The Courier to a copy of the band folder and run the
   export regression (scratchpad `cmp.py`) against the previous export folder: every PDF should be identical, and
   the log's "[starscore] layout check" lines should list the same moves/issues as 1.18.53. Then export G.I. Jorge
   and compare the "sheet time … print" laps for the Section Scores with 1.18.53's log.
5. **Data tag guard** (change 5). Open a song, change a section's status, save, reopen: the status must be kept
   and the log must have no "not storing StarScore data" line. (That line would mean the tag was unreadable, which
   should never happen on a healthy file.)
6. **Copy part formatting** (change 6). Format › Copy part formatting (both directions) on G.I. Jorge, which has the
   4-Horn 2-Trumpet: the list order must be 1H, 2H, 2H Flexible, 3H, 3H Flexible, 4H together with 4H 2-Trumpet,
   5H–7H, Rhythm, other sections, Big Band/Marching/Orchestra, multi-part books — as in 1.18.53.
7. **Update install** (unchanged, but worth a look while a dmg is being installed): Help › Check for update on the
   previous build, let it install, and confirm the "Installing" window closes and the new StarScore opens by itself;
   `~/Library/Application Support/StarScore Studio/update-in-progress` must be gone afterwards.
