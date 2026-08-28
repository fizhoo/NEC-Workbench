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
    double durationSeconds{};
};

class AnalysisRunStore final {
public:
    explicit AnalysisRunStore(QString rootDirectory = {});

    [[nodiscard]] auto create(const QString& backend, const QString& sourceFile = {}) const -> AnalysisRunRecord;
    [[nodiscard]] auto load() const -> std::vector<AnalysisRunRecord>;
    auto save(const AnalysisRunRecord& record) const -> bool;
    [[nodiscard]] auto rootDirectory() const -> QString;

private:
    [[nodiscard]] auto readRecord(const QString& directory) const -> AnalysisRunRecord;

    QString rootDirectory_;
};

}
