#include "ui/analysis/AnalysisRunStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace necwb::ui {
namespace {

auto frequenciesToJson(const std::vector<double>& frequencies) -> QJsonArray
{
    QJsonArray result;
    for (const auto frequency : frequencies) result.append(frequency);
    return result;
}

auto frequenciesFromJson(const QJsonValue& value) -> std::vector<double>
{
    std::vector<double> result;
    for (const auto frequency : value.toArray()) result.push_back(frequency.toDouble());
    return result;
}

auto radiationCoverageToJson(
    const std::vector<analysis::RadiationCoverage>& coverage) -> QJsonArray
{
    QJsonArray result;
    for (const auto& item : coverage) {
        result.append(QJsonObject{
            {QStringLiteral("frequencyMHz"), item.frequencyMHz},
            {QStringLiteral("patternIndex"), item.patternIndex},
            {QStringLiteral("sampleCount"), static_cast<qint64>(item.sampleCount)},
            {QStringLiteral("directionFingerprint"),
                QString::fromStdString(item.directionFingerprint)},
        });
    }
    return result;
}

auto radiationCoverageFromJson(const QJsonValue& value)
    -> std::vector<analysis::RadiationCoverage>
{
    std::vector<analysis::RadiationCoverage> result;
    for (const auto entry : value.toArray()) {
        const auto object = entry.toObject();
        result.push_back({
            object.value(QStringLiteral("frequencyMHz")).toDouble(),
            object.value(QStringLiteral("patternIndex")).toInt(),
            static_cast<std::size_t>(
                object.value(QStringLiteral("sampleCount")).toInteger()),
            object.value(QStringLiteral("directionFingerprint")).toString().toStdString(),
        });
    }
    return result;
}

}

AnalysisRunStore::AnalysisRunStore(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory))
{
    if (rootDirectory_.isEmpty()) {
        rootDirectory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("runs"));
    }
}

auto AnalysisRunStore::create(const QString& backend, const QString& sourceFile,
    const QString& runType, const QString& parentId) const -> AnalysisRunRecord
{
    const auto started = QDateTime::currentDateTime();
    const auto id = QStringLiteral("%1-%2")
        .arg(started.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")),
            QUuid::createUuid().toString(QUuid::Id128).left(8));
    AnalysisRunRecord record;
    record.id = id;
    record.directory = QDir(rootDirectory_).filePath(id);
    record.started = started;
    record.sourceFile = sourceFile;
    record.backend = backend;
    record.status = QStringLiteral("Starting");
    record.runType = runType;
    record.parentId = parentId;
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

auto AnalysisRunStore::loadRecent(std::size_t maximum) const
    -> std::vector<AnalysisRunRecord>
{
    std::vector<AnalysisRunRecord> records;
    if (maximum == 0) return records;
    const QDir root(rootDirectory_);
    const auto directories = root.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    records.reserve(std::min(maximum, static_cast<std::size_t>(directories.size())));
    for (const auto& directoryName : directories) {
        auto record = readRecord(root.filePath(directoryName));
        if (record.id.isEmpty()) continue;
        if (record.status == QStringLiteral("Starting")
            || record.status == QStringLiteral("Running")) {
            record.status = QStringLiteral("Interrupted");
            save(record);
        }
        records.push_back(std::move(record));
        if (records.size() == maximum) break;
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
        {QStringLiteral("version"), 6},
        {QStringLiteral("id"), record.id},
        {QStringLiteral("started"), record.started.toString(Qt::ISODateWithMs)},
        {QStringLiteral("sourceFile"), record.sourceFile},
        {QStringLiteral("backend"), record.backend},
        {QStringLiteral("status"), record.status},
        {QStringLiteral("runType"), record.runType},
        {QStringLiteral("parentId"), record.parentId},
        {QStringLiteral("summary"), record.summary},
        {QStringLiteral("userNote"), record.userNote},
        {QStringLiteral("candidateCount"), record.candidateCount},
        {QStringLiteral("durationSeconds"), record.durationSeconds},
        {QStringLiteral("outputBytes"), record.outputBytes},
        {QStringLiteral("frequencyCount"), record.frequencyCount},
        {QStringLiteral("hasImpedance"), record.hasImpedance},
        {QStringLiteral("hasCurrents"), record.hasCurrents},
        {QStringLiteral("hasRadiation"), record.hasRadiation},
        {QStringLiteral("modelFingerprint"), record.modelFingerprint},
        {QStringLiteral("referenceImpedanceOhms"), record.referenceImpedanceOhms},
        {QStringLiteral("impedanceFrequenciesMHz"),
            frequenciesToJson(record.impedanceFrequenciesMHz)},
        {QStringLiteral("radiationCoverage"),
            radiationCoverageToJson(record.radiationCoverage)},
    };
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

auto AnalysisRunStore::remove(const QString& directory) const -> bool
{
    const auto root = QDir(rootDirectory_).absolutePath();
    const auto target = QDir(directory).absolutePath();
    const auto relative = QDir(root).relativeFilePath(target);
    if (relative == QStringLiteral(".") || relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) return false;
    return QDir(target).removeRecursively();
}

auto AnalysisRunStore::removeGroup(const QString& id, const QString& directory) const -> bool
{
    auto removed = true;
    for (const auto& record : load()) {
        if (record.parentId == id) removed = remove(record.directory) && removed;
    }
    return remove(directory) && removed;
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
    record.runType = object.value(QStringLiteral("runType")).toString();
    if (record.runType.isEmpty()) {
        record.runType = QFileInfo::exists(QDir(directory).filePath(QStringLiteral("optimization.json")))
            ? QStringLiteral("optimization-candidate") : QStringLiteral("analysis");
    }
    record.parentId = object.value(QStringLiteral("parentId")).toString();
    record.summary = object.value(QStringLiteral("summary")).toString();
    record.userNote = object.value(QStringLiteral("userNote")).toString();
    record.candidateCount = object.value(QStringLiteral("candidateCount")).toInt();
    record.durationSeconds = object.value(QStringLiteral("durationSeconds")).toDouble();
    record.outputBytes = object.value(QStringLiteral("outputBytes")).toInteger();
    record.frequencyCount = object.value(QStringLiteral("frequencyCount")).toInt();
    record.hasImpedance = object.value(QStringLiteral("hasImpedance")).toBool();
    record.hasCurrents = object.value(QStringLiteral("hasCurrents")).toBool();
    record.hasRadiation = object.value(QStringLiteral("hasRadiation")).toBool();
    record.modelFingerprint = object.value(QStringLiteral("modelFingerprint")).toString();
    record.referenceImpedanceOhms = object.contains(QStringLiteral("referenceImpedanceOhms"))
        ? object.value(QStringLiteral("referenceImpedanceOhms")).toDouble() : 50.0;
    record.impedanceFrequenciesMHz = frequenciesFromJson(
        object.value(QStringLiteral("impedanceFrequenciesMHz")));
    record.radiationCoverage = radiationCoverageFromJson(
        object.value(QStringLiteral("radiationCoverage")));
    return record;
}

}
