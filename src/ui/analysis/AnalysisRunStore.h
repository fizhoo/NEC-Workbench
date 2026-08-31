#pragma once

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
    QString status;
    QString runType{QStringLiteral("analysis")};
    QString parentId;
    QString summary;
    int candidateCount{};
    double durationSeconds{};
    qint64 outputBytes{};
    int frequencyCount{};
    bool hasImpedance{};
    bool hasCurrents{};
    bool hasRadiation{};
};

class AnalysisRunStore final {
public:
    explicit AnalysisRunStore(QString rootDirectory = {});

    [[nodiscard]] auto create(const QString& backend, const QString& sourceFile = {},
        const QString& runType = QStringLiteral("analysis"), const QString& parentId = {}) const
        -> AnalysisRunRecord;
    [[nodiscard]] auto load() const -> std::vector<AnalysisRunRecord>;
    auto save(const AnalysisRunRecord& record) const -> bool;
    auto remove(const QString& directory) const -> bool;
    auto removeGroup(const QString& id, const QString& directory) const -> bool;
    [[nodiscard]] auto rootDirectory() const -> QString;

private:
    [[nodiscard]] auto readRecord(const QString& directory) const -> AnalysisRunRecord;

    QString rootDirectory_;
};

}
