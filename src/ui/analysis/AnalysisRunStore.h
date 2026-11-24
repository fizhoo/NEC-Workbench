#pragma once

#include "analysis/OptimizationBaseline.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace necwb::ui {

struct AnalysisRunRecord {
    QString id;
    QString directory;
    QDateTime started;
    QString sourceFile;
    QString backend;
    // The engine build that made the run, such as "nec2c 1.3.3 (55be1e0)", and its path.
    QString engineVersion;
    QString enginePath;
    QString status;
    QString runType{QStringLiteral("analysis")};
    QString parentId;
    QString summary;
    QString userNote;
    int candidateCount{};
    double durationSeconds{};
    qint64 outputBytes{};
    int frequencyCount{};
    bool hasImpedance{};
    bool hasCurrents{};
    bool hasRadiation{};
    QString modelFingerprint;
    double referenceImpedanceOhms{50.0};
    std::vector<double> impedanceFrequenciesMHz;
    std::vector<analysis::RadiationCoverage> radiationCoverage;
};

class AnalysisRunStore final {
public:
    explicit AnalysisRunStore(QString rootDirectory = {});

    // Records the executable's version (SolverVersions) when one is given.
    [[nodiscard]] auto create(const QString& backend, const QString& sourceFile = {},
        const QString& runType = QStringLiteral("analysis"), const QString& parentId = {},
        const QString& executable = {}) const -> AnalysisRunRecord;
    [[nodiscard]] auto load() const -> std::vector<AnalysisRunRecord>;
    [[nodiscard]] auto loadRecent(std::size_t maximum) const
        -> std::vector<AnalysisRunRecord>;
    auto save(const AnalysisRunRecord& record) const -> bool;
    auto remove(const QString& directory) const -> bool;
    auto removeGroup(const QString& id, const QString& directory) const -> bool;
    [[nodiscard]] auto rootDirectory() const -> QString;

private:
    [[nodiscard]] auto readRecord(const QString& directory) const -> AnalysisRunRecord;

    QString rootDirectory_;
};

}
