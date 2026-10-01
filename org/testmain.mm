#include <QGuiApplication>
#include <QDebug>
#include <QTimer>
#include "organizer.h"
using namespace mu::project::starscore::org;
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const QString root = argv[1];
    Paths paths = Paths::make(root + "/Sheets and Demos", root + "/Projects and Sheets", QDate(2026, 10, 1));
    auto org = Organizer::create(paths, Fetcher(), Prompts());
    RunRequest req; req.scope = RunRequest::RebuildAll; req.online = false;
    Progress prog;
    prog.log = [](const QString& l) { qDebug().noquote() << "LOG" << l; };
    prog.step = [](const QString& s, double f) { qDebug().noquote() << "STEP" << int(f * 100) << s; };
    QTimer::singleShot(0, [&]() {
        org->run(req, prog, [&](RunSummary s) {
            qDebug().noquote() << "DONE" << s.headline << s.lines << s.warnings << s.pdfsWritten << s.pdfsFailed;
            app.quit();
        });
    });
    return app.exec();
}
