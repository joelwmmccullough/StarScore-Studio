/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the organizer: one run keeps Sheets and Demos and Projects and Sheets in order and rebuilds
 * the generated PDFs that are out of date. It runs after "Export to Sheets and Demos" (unless its tick box is off),
 * from "Run Folder Organization", and from "Rebuild everything".
 *
 * A run, in order:
 *  1. setlist.fm (main thread, asks Joel about new shows)       orgrecordings
 *  2. filing, codes, measuring, changelogs, the HTML (worker)  orgfiling, orgscan, orgchangelog, orghtml*
 *  3. printing the HTML to PDF (main thread, WebKit)            orgplatform_mac
 *  4. putting the PDFs in place, saving the data files (worker), folder colours
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "orgchangelog.h"
#include "orgcore.h"
#include "orgrecordings.h"

namespace mu::project::starscore::org {
struct RunRequest {
    enum Scope { AfterExport, Organize, RebuildAll } scope = Organize;
    std::optional<ExportInfo> exported;
    bool online = true;                 // look at setlist.fm
};

struct RunSummary {
    bool ok = true;
    bool stopped = false;
    QString headline;                   // one line for the dialog
    QStringList lines;                  // what happened (plain text)
    QStringList warnings;               // needs Joel (plain text)
    int pdfsWritten = 0;
    int pdfsFailed = 0;
    QStringList changedCodes;           // songs whose recordings changed (open .starscore files refresh their copy)
};

//! The windows Joel answers. Both are called on the main thread and must call their continuation exactly once.
struct Prompts {
    std::function<void(const std::vector<NewShow>&, std::function<void(std::vector<ShowAnswer>)>)> askShows;
    //! Joel checks the songs found in the videos' descriptions (fixes codes in place; "-" = not a song)
    std::function<void(std::vector<ShowVideo>&, const QStringList& songChoices, std::function<void()>)> confirmSongs;
};

class Organizer : public std::enable_shared_from_this<Organizer>
{
public:
    static std::shared_ptr<Organizer> create(const Paths& paths, Fetcher fetch, Prompts prompts);

    //! done is called on the main thread
    void run(const RunRequest& request, const Progress& progress, std::function<void(RunSummary)> done);

    //! "Stop": finishes the step it is in, saves nothing half-done
    void stop();
    std::atomic_bool& stopFlag() { return m_stop; }

    //! Tests (no WebKit): each "PDF" holds its HTML, and a copy named after the PDF goes to previewFolder
    void setWriteHtmlInstead(const QString& previewFolder) { m_htmlInstead = previewFolder; }

private:
    Organizer(const Paths& paths, Fetcher fetch, Prompts prompts);
    struct Run;
    void online(std::shared_ptr<Run> r);
    void prepare(std::shared_ptr<Run> r);
    void render(std::shared_ptr<Run> r);
    void deploy(std::shared_ptr<Run> r);
    void finish(std::shared_ptr<Run> r);
    void saveCodesAndRecordings(std::shared_ptr<Run> r);

    Paths m_paths;
    Fetcher m_fetch;
    Prompts m_prompts;
    std::atomic_bool m_stop { false };
    QString m_htmlInstead;
};

//! Run fn on the main thread (from a worker)
void onMainThread(std::function<void()> fn);
//! Run work on a new thread, then then() on the main thread
void inBackground(std::function<void()> work, std::function<void()> then);
}
