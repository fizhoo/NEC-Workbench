#include "ui/analysis/ResultReferenceController.h"

#include "analysis/NecOutputParser.h"
#include "analysis/ReferenceRunSelection.h"
#include "model/ModelSetup.h"
#include "nec/NecParser.h"
#include "nec/NecSetupConverter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace necwb::ui {
namespace {

auto readFile(const QString& path) -> QByteArray
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

}

ResultReferenceController::ResultReferenceController(const AnalysisRunStore& store)
    : store_(store)
{
}

void ResultReferenceController::selectAutomatic(const AnalysisRunRecord& activeRun)
{
    if (pinned_) return;
    const auto identity = modelIdentity(activeRun.sourceFile);
    const auto records = store_.load();
    std::vector<analysis::ReferenceRunCandidate> candidates;
    candidates.reserve(records.size());
    for (const auto& record : records) {
        candidates.push_back({record.id.toStdString(),
            modelIdentity(record.sourceFile).toStdString(), record.runType.toStdString(),
            record.status.toStdString(), record.started.toMSecsSinceEpoch()});
    }
    const auto selected = analysis::selectPreviousReferenceRun(candidates,
        activeRun.id.toStdString(), identity.toStdString());
    if (!selected) {
        clear();
        return;
    }
    select(records[*selected], false);
}

void ResultReferenceController::pin(const AnalysisRunRecord& run)
{
    select(run, true);
}

void ResultReferenceController::clear()
{
    reference_.reset();
    parsedResult_.reset();
    parseAttempted_ = false;
    pinned_ = false;
}

auto ResultReferenceController::hasReference() const -> bool
{
    return reference_.has_value();
}

auto ResultReferenceController::isPinned() const -> bool
{
    return pinned_;
}

auto ResultReferenceController::record() const -> const AnalysisRunRecord*
{
    return reference_ ? &*reference_ : nullptr;
}

auto ResultReferenceController::result() const -> const analysis::AnalysisResult*
{
    if (!reference_) return nullptr;
    if (parseAttempted_) return parsedResult_ ? &*parsedResult_ : nullptr;
    parseAttempted_ = true;

    const auto output = readFile(QDir(reference_->directory).filePath(QStringLiteral("model.out")));
    if (output.isEmpty()) return nullptr;
    auto parsed = analysis::NecOutputParser{}.parse(
        std::string_view(output.constData(), static_cast<std::size_t>(output.size())));
    const auto source = readFile(
        QDir(reference_->directory).filePath(QStringLiteral("model.source.nec")));
    if (!source.isEmpty()) {
        const auto document = nec::NecParser{}.parse(
            std::string_view(source.constData(), static_cast<std::size_t>(source.size())));
        parsed.referenceImpedanceOhms = model::referenceImpedanceOhms(
            nec::NecSetupConverter{}.convert(document));
    }
    parsedResult_ = std::move(parsed);
    return &*parsedResult_;
}

auto ResultReferenceController::label() const -> QString
{
    if (!reference_) return {};
    const auto modelName = reference_->sourceFile.isEmpty()
        ? QStringLiteral("Archived model.nec") : QFileInfo(reference_->sourceFile).fileName();
    const auto started = reference_->started.isValid()
        ? reference_->started.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : reference_->id;
    return QStringLiteral("%1 — %2 — %3%4")
        .arg(modelName, started, reference_->backend,
            pinned_ ? QStringLiteral(" — pinned") : QString{});
}

auto ResultReferenceController::modelIdentity(const QString& sourceFile) -> QString
{
    if (sourceFile.isEmpty() || sourceFile == QStringLiteral("Untitled model.nec")) return {};
    return QDir::cleanPath(QFileInfo(sourceFile).absoluteFilePath());
}

void ResultReferenceController::select(AnalysisRunRecord run, bool pinned)
{
    reference_ = std::move(run);
    parsedResult_.reset();
    parseAttempted_ = false;
    pinned_ = pinned;
}

}
