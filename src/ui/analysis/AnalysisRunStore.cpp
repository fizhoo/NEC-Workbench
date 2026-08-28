#include "ui/analysis/AnalysisRunStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace necwb::ui {

AnalysisRunStore::AnalysisRunStore(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory))
{
    if (rootDirectory_.isEmpty()) {
        rootDirectory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("runs"));
    }
}

auto AnalysisRunStore::create(const QString& backend, const QString& sourceFile) const -> AnalysisRunRecord
{
    const auto started = QDateTime::currentDateTime();
    const auto id = QStringLiteral("%1-%2")
        .arg(started.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
            QUuid::createUuid().toString(QUuid::Id128).left(8));
    AnalysisRunRecord record{id, QDir(rootDirectory_).filePath(id), started,
        sourceFile, backend, QStringLiteral("Starting"), 0.0};
    QDir().mkpath(record.directory);
    save(record);
    return record;
}

auto AnalysisRunStore::load() const -> std::vector<AnalysisRunRecord>
{
    std::vector<AnalysisRunRecord> records;
    const QDir root(rootDirectory_);
    for (const auto& directoryName : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        auto record = readRecord(root.filePath(directoryName));
        if (record.id.isEmpty()) {
            continue;
        }
        if (record.status == QStringLiteral("Starting") || record.status == QStringLiteral("Running")) {
            record.status = QStringLiteral("Interrupted");
            save(record);
        }
        records.push_back(std::move(record));
    }
    std::ranges::sort(records, std::greater{}, &AnalysisRunRecord::started);
    return records;
}

auto AnalysisRunStore::save(const AnalysisRunRecord& record) const -> bool
{
    if (!QDir().mkpath(record.directory)) {
        return false;
    }
    QFile file(QDir(record.directory).filePath(QStringLiteral("run.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonObject object{
        {QStringLiteral("version"), 2},
        {QStringLiteral("id"), record.id},
        {QStringLiteral("started"), record.started.toString(Qt::ISODateWithMs)},
        {QStringLiteral("sourceFile"), record.sourceFile},
        {QStringLiteral("backend"), record.backend},
        {QStringLiteral("status"), record.status},
        {QStringLiteral("durationSeconds"), record.durationSeconds},
    };
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

auto AnalysisRunStore::rootDirectory() const -> QString
{
    return rootDirectory_;
}

auto AnalysisRunStore::readRecord(const QString& directory) const -> AnalysisRunRecord
{
    QFile file(QDir(directory).filePath(QStringLiteral("run.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    AnalysisRunRecord record;
    record.id = object.value(QStringLiteral("id")).toString();
    record.directory = directory;
    record.started = QDateTime::fromString(
        object.value(QStringLiteral("started")).toString(), Qt::ISODateWithMs);
    record.sourceFile = object.value(QStringLiteral("sourceFile")).toString();
    record.backend = object.value(QStringLiteral("backend")).toString();
    record.status = object.value(QStringLiteral("status")).toString();
    record.durationSeconds = object.value(QStringLiteral("durationSeconds")).toDouble();
    return record;
}

}
