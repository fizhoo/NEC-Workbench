// Checks that the bundled nec2c computes what a reference build does.
//
//   necwb_nec2c_check decks <skip-list> <output-dir> <deck-dir>...
//     Writes the deck Workbench would send nec2c for every *.nec in the deck folders, under
//     a name nec2c accepts. A deck that does not resolve or check must be on the skip list,
//     and every deck on it must fail, so the list stays reviewed.
//   necwb_nec2c_check compare --strict|--tolerance <deck-dir> <output-a> <output-b>
//     Compares <name>.out for every deck. --strict compares the text, leaving out the run
//     time lines; --tolerance compares the parsed impedances, currents and gains. Both fail
//     on a missing or empty output.

#include "analysis/NecOutputParser.h"
#include "analysis/SolverCommand.h"
#include "analysis/SolverInput.h"
#include "nec/NecModelChecker.h"
#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

auto readFile(const fs::path& path) -> std::string
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

auto readSkipList(const fs::path& path) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> skipped;
    std::istringstream lines(readFile(path));
    for (std::string line; std::getline(lines, line);) {
        const auto hash = line.find('#');
        auto name = line.substr(0, hash);
        name.erase(name.find_last_not_of(" \t\r") + 1);
        if (!name.empty()) skipped[name] = hash == std::string::npos ? "" : line.substr(hash + 1);
    }
    return skipped;
}

// Short and without spaces: nec2c takes file names of at most 75 characters.
auto deckName(const fs::path& source) -> std::string
{
    auto name = source.stem().string();
    std::ranges::replace(name, ' ', '_');
    return name;
}

auto writeDecks(const fs::path& skipList, const fs::path& output, const std::vector<fs::path>& folders)
    -> int
{
    auto skipped = readSkipList(skipList);
    fs::create_directories(output);
    std::vector<fs::path> sources;
    for (const auto& folder : folders)
        for (const auto& entry : fs::directory_iterator(folder))
            if (entry.path().extension() == ".nec") sources.push_back(entry.path());
    std::ranges::sort(sources);
    auto failures = 0;
    auto written = 0;
    for (const auto& source : sources) {
        const auto file = source.filename().string();
        const auto resolution = necwb::nec::NecSymbolResolver{}.resolve(readFile(source));
        std::string problem;
        if (!resolution.ok()) {
            problem = "does not resolve";
        } else {
            const auto check = necwb::nec::NecModelChecker{}.check(
                necwb::nec::NecParser{}.parse(resolution.generatedDeck),
                {necwb::nec::compatibilityProfileForBackend("nec2")});
            if (check.errorCount() != 0) problem = "has model check errors";
        }
        const auto listed = skipped.erase(file) == 1;
        if (!problem.empty()) {
            std::cout << (listed ? "skipped " : "FAIL ") << file << ": " << problem << '\n';
            failures += listed ? 0 : 1;
            continue;
        }
        if (listed) {
            std::cout << "FAIL " << file << " is on the skip list but runs; take it off\n";
            ++failures;
        }
        std::ofstream(output / (deckName(source) + ".nec"), std::ios::binary)
            << necwb::analysis::formatSolverInputForBackend(
                   necwb::analysis::normalizeSolverDeck(resolution.generatedDeck), "nec2");
        ++written;
    }
    for (const auto& [file, reason] : skipped) {
        std::cout << "FAIL " << file << " is on the skip list but was not found\n";
        ++failures;
    }
    std::cout << written << " decks written to " << output.string() << '\n';
    return failures == 0 && written > 0 ? 0 : 1;
}

// The text without the run time lines and line-end differences.
auto comparableText(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        line.erase(line.find_last_not_of(" \r") + 1);
        if (line.find("msec") == std::string::npos) lines.push_back(line);
    }
    return lines;
}

struct Differences {
    std::vector<std::string> lines;
    void add(std::string text) { lines.push_back(std::move(text)); }
};

auto describe(const char* what, double frequency, int index, double first, double second) -> std::string
{
    char text[200];
    std::snprintf(text, sizeof text, "%s at %.6g MHz, #%d: %.10g vs %.10g", what, frequency, index,
        first, second);
    return text;
}

// Relative to the larger value, with an absolute floor for values near zero.
auto close(double first, double second, double relative, double floor) -> bool
{
    return std::abs(first - second) <= std::max(relative * std::max(std::abs(first), std::abs(second)), floor);
}

auto phaseClose(double first, double second, double tolerance) -> bool
{
    const auto difference = std::remainder(first - second, 360.0);
    return std::abs(difference) <= tolerance;
}

// Returns what was compared, as "3 frequencies, 93 segment currents, 222 pattern points".
auto compareParsed(const std::string& first, const std::string& second, Differences& differences)
    -> std::string
{
    constexpr auto Relative = 1e-4;
    constexpr auto GainDb = 0.02;
    constexpr auto PhaseDegrees = 0.01;
    // Below this both sides are a null, printed as -999.99 or a very small gain.
    constexpr auto NullDb = -100.0;
    const auto a = necwb::analysis::NecOutputParser{}.parse(first);
    const auto b = necwb::analysis::NecOutputParser{}.parse(second);
    const auto frequencies = [](const auto& result) {
        std::set<double> found;
        for (const auto& feed : result.feedpoints) found.insert(feed.frequencyMHz);
        return found.size();
    };
    if (a.feedpoints.size() != b.feedpoints.size() || a.currents.size() != b.currents.size()
        || a.radiation.size() != b.radiation.size() || frequencies(a) != frequencies(b)) {
        differences.add("counts differ: frequencies " + std::to_string(frequencies(a)) + "/"
            + std::to_string(frequencies(b)) + ", feedpoints " + std::to_string(a.feedpoints.size())
            + "/" + std::to_string(b.feedpoints.size()) + ", segment currents "
            + std::to_string(a.currents.size()) + "/" + std::to_string(b.currents.size())
            + ", pattern points " + std::to_string(a.radiation.size()) + "/"
            + std::to_string(b.radiation.size()));
        return {};
    }
    if (a.feedpoints.empty()) differences.add("no feedpoint results");
    for (std::size_t index = 0; index < a.feedpoints.size(); ++index) {
        const auto& x = a.feedpoints[index];
        const auto& y = b.feedpoints[index];
        const auto floor = 1e-3;
        if (!close(x.impedance.real(), y.impedance.real(), Relative, floor)
            || !close(x.impedance.imag(), y.impedance.imag(), Relative, floor))
            differences.add(describe("impedance", x.frequencyMHz, static_cast<int>(index),
                std::abs(x.impedance), std::abs(y.impedance)) + " (R " + std::to_string(x.impedance.real())
                + "/" + std::to_string(y.impedance.real()) + ", X " + std::to_string(x.impedance.imag())
                + "/" + std::to_string(y.impedance.imag()) + ")");
    }
    // Currents near zero, such as at open wire ends, are compared against the largest
    // current at that frequency, and their phase only where they are not near zero.
    std::map<double, double> largest;
    for (const auto& current : a.currents)
        largest[current.frequencyMHz] = std::max(largest[current.frequencyMHz], current.magnitude);
    for (std::size_t index = 0; index < a.currents.size(); ++index) {
        const auto& x = a.currents[index];
        const auto& y = b.currents[index];
        const auto scale = largest[x.frequencyMHz];
        if (x.segment != y.segment || !close(x.magnitude, y.magnitude, Relative, 1e-6 * scale))
            differences.add(describe("current magnitude", x.frequencyMHz, x.segment, x.magnitude, y.magnitude));
        else if (x.magnitude > 1e-3 * scale && !phaseClose(x.phaseDegrees, y.phaseDegrees, PhaseDegrees))
            differences.add(describe("current phase", x.frequencyMHz, x.segment, x.phaseDegrees, y.phaseDegrees));
    }
    for (std::size_t index = 0; index < a.radiation.size(); ++index) {
        const auto& x = a.radiation[index];
        const auto& y = b.radiation[index];
        const auto gainsClose = [&](double first, double second) {
            return (first < NullDb && second < NullDb) || std::abs(first - second) <= GainDb;
        };
        if (x.thetaDegrees != y.thetaDegrees || x.phiDegrees != y.phiDegrees
            || !gainsClose(x.verticalGainDb, y.verticalGainDb)
            || !gainsClose(x.horizontalGainDb, y.horizontalGainDb)
            || !gainsClose(x.totalGainDb, y.totalGainDb))
            differences.add(describe("total gain", x.frequencyMHz, static_cast<int>(index),
                x.totalGainDb, y.totalGainDb) + " at theta " + std::to_string(x.thetaDegrees)
                + ", phi " + std::to_string(x.phiDegrees));
    }
    return std::to_string(frequencies(a)) + " frequencies, " + std::to_string(a.feedpoints.size())
        + " feedpoints, " + std::to_string(a.currents.size()) + " segment currents, "
        + std::to_string(a.radiation.size()) + " pattern points";
}

auto compare(bool strict, const fs::path& decks, const fs::path& first, const fs::path& second) -> int
{
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(decks))
        if (entry.path().extension() == ".nec") names.push_back(entry.path().stem().string());
    std::ranges::sort(names);
    auto failed = 0;
    for (const auto& name : names) {
        const auto a = readFile(first / (name + ".out"));
        const auto b = readFile(second / (name + ".out"));
        Differences differences;
        std::string compared;
        if (a.empty() || b.empty()) {
            differences.add(std::string("missing or empty output in ")
                + (a.empty() ? first : second).string());
        } else if (strict) {
            const auto x = comparableText(a);
            const auto y = comparableText(b);
            if (x.size() != y.size()) differences.add("line counts differ: " + std::to_string(x.size())
                + " vs " + std::to_string(y.size()));
            for (std::size_t line = 0; line < std::min(x.size(), y.size()); ++line)
                if (x[line] != y[line]) differences.add("line " + std::to_string(line + 1) + ":\n  " + x[line]
                    + "\n  " + y[line]);
        } else {
            compared = compareParsed(a, b, differences);
        }
        std::cout << (differences.lines.empty() ? "same " : "DIFFERENT ") << name
                  << (compared.empty() ? "" : " (" + compared + ")") << '\n';
        for (std::size_t index = 0; index < std::min<std::size_t>(differences.lines.size(), 20); ++index)
            std::cout << "  " << differences.lines[index] << '\n';
        if (differences.lines.size() > 20)
            std::cout << "  … and " << differences.lines.size() - 20 << " more\n";
        failed += differences.lines.empty() ? 0 : 1;
    }
    std::cout << names.size() - failed << " of " << names.size() << " decks match ("
              << (strict ? "text" : "parsed numbers within tolerance") << ")\n";
    return failed == 0 && !names.empty() ? 0 : 1;
}

}

auto main(int argc, char* argv[]) -> int
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);
    if (arguments.size() >= 4 && arguments[0] == "decks")
        return writeDecks(arguments[1], arguments[2], {arguments.begin() + 3, arguments.end()});
    if (arguments.size() == 5 && arguments[0] == "compare"
        && (arguments[1] == "--strict" || arguments[1] == "--tolerance"))
        return compare(arguments[1] == "--strict", arguments[2], arguments[3], arguments[4]);
    std::cerr << "usage: necwb_nec2c_check decks <skip-list> <output-dir> <deck-dir>...\n"
                 "       necwb_nec2c_check compare --strict|--tolerance <deck-dir> <output-a> <output-b>\n";
    return 2;
}
