#include "ui/setup/SetupLine.h"

#include "model/ModelSummary.h"
#include "ui/analysis/AnalysisSetupEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#include <algorithm>

namespace necwb::ui {
namespace {

// The line's numbers as short as they are exact: 144, 14.175.
auto compact(double value) -> QString
{
    return QString::number(value, 'g', 7);
}

constexpr std::array Parts{ModelNode::Frequency, ModelNode::Ground, ModelNode::Requests,
    ModelNode::Solver, ModelNode::ReferenceImpedance};

auto groundText(const std::optional<model::GroundDefinition>& ground) -> std::pair<QString, QString>
{
    const auto type = ground ? ground->type : model::GroundType::FreeSpace;
    const auto real = ground ? QObject::tr("εr %1, σ %2 S/m").arg(
        compact(ground->relativePermittivity), compact(ground->conductivity)) : QString{};
    switch (type) {
    case model::GroundType::FreeSpace: return {QObject::tr("Free space"), QObject::tr("No ground")};
    case model::GroundType::Perfect:
        return {QObject::tr("Perfect ground"), QObject::tr("Perfectly conducting ground")};
    case model::GroundType::SommerfeldNorton:
        return {QObject::tr("Sommerfeld"), QObject::tr("Real ground, Sommerfeld/Norton: %1").arg(real)};
    case model::GroundType::ReflectionApproximation:
        return {QObject::tr("Reflection"),
            QObject::tr("Real ground, reflection approximation: %1").arg(real)};
    }
    return {QObject::tr("Ground"), {}};
}

}

SetupLine::SetupLine(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("setupLine"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(0);
    const std::array names{"setupFrequency", "setupGround", "setupRequests", "setupSolver",
        "setupReferenceImpedance"};
    for (auto index = std::size_t{0}; index < parts_.size(); ++index) {
        if (index > 0) {
            auto* separator = new QLabel(QStringLiteral("|"), this);
            separator->setForegroundRole(QPalette::PlaceholderText);
            separator->setContentsMargins(2, 0, 2, 0);
            layout->addWidget(separator);
        }
        auto* part = new QToolButton(this);
        part->setObjectName(QString::fromLatin1(names[index]));
        part->setAutoRaise(true);
        part->setCheckable(true);
        part->setFocusPolicy(Qt::TabFocus);
        connect(part, &QToolButton::clicked, this, [this, index] {
            // The pane decides what shows; setCurrent marks it.
            parts_[index]->setChecked(!parts_[index]->isChecked());
            if (clicked_) clicked_(Parts[index]);
        });
        parts_[index] = part;
        layout->addWidget(part);
    }
    layout->addStretch(1);
    setPart(ModelNode::Frequency, tr("No frequency"), {});
    setPart(ModelNode::Ground, tr("Free space"), {});
    setPart(ModelNode::Requests, tr("No patterns"), {});
    setPart(ModelNode::Solver, tr("Solver"), {});
    setPart(ModelNode::ReferenceImpedance, tr("Z0 50 Ω"), {});
}

void SetupLine::setPart(ModelNode part, const QString& text, const QString& toolTip)
{
    auto* button = parts_[static_cast<std::size_t>(std::ranges::find(Parts, part) - Parts.begin())];
    button->setText(text);
    button->setToolTip(toolTip.isEmpty() ? text : toolTip);
}

void SetupLine::setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
    const nec::NecDocument& document)
{
    const auto summary = model::summarizeModel(model, setup, document);
    if (const auto& frequency = summary.frequency) {
        const auto sweep = frequency->pointCount > 1;
        const auto text = sweep ? tr("%1–%2 MHz · %3 pts")
                .arg(compact(frequency->startMHz), compact(frequency->endMHz))
                .arg(frequency->pointCount)
            : tr("%1 MHz").arg(compact(frequency->startMHz));
        setPart(ModelNode::Frequency, text, sweep && setup.frequency
            ? tr("FR: %1, %2 %3").arg(text, setup.frequency->steppingMode == 1
                    ? tr("multiplier") : tr("step"), compact(setup.frequency->step))
            : tr("FR: %1").arg(text));
    } else {
        setPart(ModelNode::Frequency, tr("No frequency"), tr("No FR card: click to add one"));
    }
    const auto [ground, groundDetail] = groundText(setup.ground);
    setPart(ModelNode::Ground, ground, groundDetail);
    const auto& patterns = summary.radiationRequests;
    setPart(ModelNode::Requests, patterns.empty()
            ? summary.executionRequestCount == 0 ? tr("No patterns") : tr("Execution only")
            : patterns.size() == 1
            ? tr("Pattern %1×%2").arg(patterns[0].thetaCount).arg(patterns[0].phiCount)
            : tr("%1 patterns").arg(patterns.size()),
        tr("%1 radiation pattern (RP) and %2 execution (XQ) requests")
            .arg(patterns.size()).arg(summary.executionRequestCount));
    const auto ohms = model::referenceImpedanceOhms(setup);
    setPart(ModelNode::ReferenceImpedance, tr("Z0 %1 Ω").arg(compact(ohms)),
        setup.referenceImpedance ? tr("Reference impedance for SWR, from the Z0 card")
                                 : tr("Reference impedance for SWR: the default 50 Ω"));
}

void SetupLine::setSolver(const QString& backendId, const QString& executable)
{
    const auto backends = AnalysisSetupEditor::backends();
    const auto backend = std::ranges::find(backends, backendId, &std::pair<QString, QString>::second);
    // "NEC-2 / nec2c" shows as nec2c.
    const auto name = backend == backends.end() ? backendId
                                                : backend->first.section(QStringLiteral(" / "), -1);
    setPart(ModelNode::Solver, executable.isEmpty() ? tr("%1 · no executable").arg(name) : name,
        executable.isEmpty() ? tr("No solver executable: click to choose one")
                             : tr("%1: %2").arg(name, executable));
}

void SetupLine::setCurrent(ModelNode node)
{
    for (auto index = std::size_t{0}; index < parts_.size(); ++index)
        parts_[index]->setChecked(Parts[index] == node);
}

void SetupLine::setClickedCallback(std::function<void(ModelNode)> callback)
{
    clicked_ = std::move(callback);
}

auto SetupLine::minimumSizeHint() const -> QSize
{
    return {0, QWidget::minimumSizeHint().height()};
}

}
