#include "experimentrunner.h"
#include "src/core/simulationstore.h"
#include "src/core/jsoncodec.h"
#include "src/core/simulationreport.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"output", "Output directory", "directory", "build/experiments/results"});
    parser.addOption({"repetitions", "Runs per case (six cases)", "count", "20"});
    parser.addOption({"config", "Configuration JSON", "path"});
    parser.addOption({"replay", "Replay saved state and export a report (no batch)", "path"});
    parser.process(app);
    try {
        QDir dir(parser.value("output"));
        if (!dir.mkpath(".")) throw std::runtime_error("Cannot create output directory");
        if (parser.isSet("replay")) {
            auto engine = SimulationStore::load(parser.value("replay"));
            JsonCodec::write(dir.filePath("replayed-report.json"), SimulationReport::create(*engine));
            return 0;
        }
        CalendarSetup setup;
        setup.startDate = QDate(2026, 10, 12); // Fixed experimental date, never wall-clock time.
        if (parser.isSet("config")) setup = SimulationStore::loadConfiguration(parser.value("config"));
        bool ok; int repetitions = parser.value("repetitions").toInt(&ok);
        if (!ok) throw std::runtime_error("Invalid repetitions");
        ExperimentRunner runner(ExperimentRunner::comparison(setup), repetitions);
        int completed = 0;
        while (!runner.finished()) {
            runner.advance();
            if (runner.completedRuns() != completed) { completed = runner.completedRuns(); QTextStream(stdout) << completed << '/' << runner.totalRuns() << '\n'; }
        }
        JsonCodec::write(dir.filePath("comparison.json"), runner.report());
        JsonCodec::writeBytes(dir.filePath("comparison.csv"), runner.csv());
        SimulationStore::saveConfiguration(dir.filePath("configuration.json"), setup);
        SimulationEngine example(setup);
        while (!example.finished()) example.step();
        SimulationStore::save(dir.filePath("example-state.json"), example);
        const auto report = SimulationReport::create(example);
        JsonCodec::write(dir.filePath("example-report.json"), report);
        JsonCodec::writeBytes(dir.filePath("example-report.csv"), SimulationReport::csv(report));
        return 0;
    } catch (const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; return 1; }
}
