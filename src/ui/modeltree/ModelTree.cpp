#include "ui/modeltree/ModelTree.h"

#include "model/ModelSummary.h"
#include "model/WireGeometry.h"
#include "nec/NecDocument.h"
#include "ui/DisplayFormat.h"
#include "ui/ElidedLabel.h"
#include "ui/WorkbenchIcon.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QToolButton>

namespace necwb::ui {
namespace {

constexpr auto NodeRole = Qt::UserRole;
constexpr auto WireTagRole = Qt::UserRole + 1;
constexpr auto SourceLineRole = Qt::UserRole + 2;
constexpr auto NameColumn = 0;
constexpr auto SummaryColumn = 1;

auto groundName(model::GroundType ground) -> QString
{
    switch (ground) {
    case model::GroundType::FreeSpace: return QObject::tr("Free space");
    case model::GroundType::Perfect: return QObject::tr("Perfect");
    case model::GroundType::SommerfeldNorton: return QObject::tr("Sommerfeld/Norton");
    case model::GroundType::ReflectionApproximation:
        return QObject::tr("Reflection approximation");
    }
    return QObject::tr("Unknown");
}

auto countText(std::size_t count, const QString& singular, const QString& plural) -> QString
{
    return count == 0 ? QObject::tr("none")
        : count == 1 ? QObject::tr("1 %1").arg(singular)
        : QObject::tr("%1 %2").arg(static_cast<qulonglong>(count)).arg(plural);
}

auto loadTypeName(int type) -> QString
{
    switch (type) {
    case 0: return QObject::tr("Series RLC");
    case 1: return QObject::tr("Parallel RLC");
    case 2: return QObject::tr("Series RLC per length");
    case 3: return QObject::tr("Parallel RLC per length");
    case 4: return QObject::tr("Impedance");
    case 5: return QObject::tr("Conductivity");
    default: return QObject::tr("LD %1").arg(type);
    }
}

auto segmentPair(int wire1, int segment1, int wire2, int segment2) -> QString
{
    return QObject::tr("W%1 S%2 ↔ W%3 S%4").arg(wire1).arg(segment1).arg(wire2).arg(segment2);
}

}

ModelTree::ModelTree(QWidget* parent) : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("modelTree"));
    setColumnCount(2);
    setHeaderHidden(true);
    setUniformRowHeights(true);
    setRootIsDecorated(true);
    setIndentation(14);
    setEditTriggers(NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    header()->setStretchLastSection(true);
    header()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    header()->setMinimumSectionSize(40);

    addGroup(ModelNode::Frequency, tr("Frequency"), "frequency-sweep");
    addGroup(ModelNode::Wires, tr("Wires"), "geometry");
    addGroup(ModelNode::Sources, tr("Sources"), "sources");
    addGroup(ModelNode::Loads, tr("Loads"), "loads");
    addGroup(ModelNode::TransmissionLines, tr("Trans. lines"), "transmission-lines");
    addGroup(ModelNode::Networks, tr("Networks"), "networks");
    addGroup(ModelNode::Ground, tr("Ground"), "ground");
    addGroup(ModelNode::Variables, tr("Variables"), "variables");
    addGroup(ModelNode::Requests, tr("Requests"), "requests");
    addGroup(ModelNode::Solver, tr("Solver"), "settings");
    addGroup(ModelNode::Deck, tr("Deck"), "nec-deck");
    auto* validation = addGroup(ModelNode::Validation, tr("Validation"), "validate");
    const std::array studies{std::pair{ModelNode::AverageGainTest, tr("Avg gain test")},
        std::pair{ModelNode::Convergence, tr("Convergence")}};
    for (auto index = std::size_t{0}; index < studies.size(); ++index) {
        auto* item = addChild(validation, {studies[index].first}, studies[index].second, {});
        auto* cell = new QWidget(this);
        auto* layout = new QHBoxLayout(cell);
        layout->setContentsMargins(0, 0, 2, 0);
        layout->setSpacing(4);
        studyLabels_[index] = new ElidedLabel(tr("not run"), cell);
        studyLabels_[index]->setObjectName(index == 0
            ? QStringLiteral("modelTreeAverageGainState")
            : QStringLiteral("modelTreeConvergenceState"));
        studyButtons_[index] = new QToolButton(cell);
        studyButtons_[index]->setObjectName(index == 0
            ? QStringLiteral("modelTreeAverageGainRun")
            : QStringLiteral("modelTreeConvergenceRun"));
        studyButtons_[index]->setText(tr("Run"));
        studyButtons_[index]->setAutoRaise(true);
        layout->addWidget(studyLabels_[index], 1);
        layout->addWidget(studyButtons_[index]);
        setItemWidget(item, SummaryColumn, cell);
    }
    validation->setExpanded(true);
    setSummary(ModelNode::Validation, {});

    connect(this, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
            if (selecting_ || current == nullptr) return;
            const auto selection = selectionOf(current);
            if (previous != nullptr && selectionChanging_ && !selectionChanging_(selection)) {
                // Stay on the row with the unapplied edit.
                selecting_ = true;
                setCurrentItem(previous);
                selecting_ = false;
                return;
            }
            if (selectionChanged_) selectionChanged_(selection);
        });
    const auto activate = [this](QTreeWidgetItem* item) {
        if (item != nullptr && item == currentItem() && activated_)
            activated_(selectionOf(item));
    };
    connect(this, &QTreeWidget::itemClicked, this, activate);
    connect(this, &QTreeWidget::itemActivated, this, activate);
    // The column gives the tree as many rows as are showing.
    connect(this, &QTreeWidget::itemExpanded, this, [this] { updateGeometry(); });
    connect(this, &QTreeWidget::itemCollapsed, this, [this] { updateGeometry(); });
}

auto ModelTree::addGroup(ModelNode node, const QString& name, const char* icon)
    -> QTreeWidgetItem*
{
    auto* item = new QTreeWidgetItem(this, {name, QString::fromUtf8("—")});
    item->setData(NameColumn, NodeRole, static_cast<int>(node));
    item->setIcon(NameColumn, workbenchIcon(icon));
    auto font = item->font(NameColumn);
    font.setBold(true);
    item->setFont(NameColumn, font);
    return item;
}

auto ModelTree::addChild(QTreeWidgetItem* group, ModelSelection selection, const QString& name,
    const QString& summary) -> QTreeWidgetItem*
{
    auto* item = new QTreeWidgetItem(group, {name, summary});
    item->setData(NameColumn, NodeRole, static_cast<int>(selection.node));
    item->setData(NameColumn, WireTagRole, selection.wireTag);
    item->setData(NameColumn, SourceLineRole, static_cast<qulonglong>(selection.sourceLine));
    item->setToolTip(SummaryColumn, summary);
    return item;
}

auto ModelTree::group(ModelNode node) const -> QTreeWidgetItem*
{
    for (auto index = 0; index < topLevelItemCount(); ++index) {
        if (topLevelItem(index)->data(NameColumn, NodeRole).toInt() == static_cast<int>(node))
            return topLevelItem(index);
    }
    return nullptr;
}

auto ModelTree::selectionOf(const QTreeWidgetItem* item) -> ModelSelection
{
    return {static_cast<ModelNode>(item->data(NameColumn, NodeRole).toInt()),
        item->parent() == nullptr ? -1 : item->data(NameColumn, WireTagRole).toInt(),
        item->parent() == nullptr ? 0
            : static_cast<std::size_t>(item->data(NameColumn, SourceLineRole).toULongLong())};
}

auto ModelTree::itemFor(const ModelSelection& selection) const -> QTreeWidgetItem*
{
    for (QTreeWidgetItemIterator it(const_cast<ModelTree*>(this)); *it != nullptr; ++it) {
        if (selectionOf(*it) == selection) return *it;
    }
    return nullptr;
}

void ModelTree::setSummary(ModelNode node, const QString& summary)
{
    if (auto* item = group(node)) {
        item->setText(SummaryColumn, summary);
        item->setToolTip(SummaryColumn, summary);
    }
}

void ModelTree::setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
    const nec::NecDocument& document)
{
    const auto summary = model::summarizeModel(model, setup, document);
    setSummary(ModelNode::Frequency, summary.frequency
        ? summary.frequency->pointCount > 1
            ? tr("%1–%2 MHz · %3 pts")
                  .arg(formatDecimal(summary.frequency->startMHz),
                      formatDecimal(summary.frequency->endMHz))
                  .arg(summary.frequency->pointCount)
            : tr("%1 MHz").arg(formatDecimal(summary.frequency->startMHz))
        : tr("not configured"));
    setSummary(ModelNode::Wires, tr("%1 · %2")
        .arg(countText(summary.wireCount, tr("wire"), tr("wires")),
            countText(summary.segmentCount, tr("segment"), tr("segments"))));
    setSummary(ModelNode::Sources, summary.sources.empty() ? tr("none")
        : summary.sources.size() == 1
            ? tr("1 source · wire %1, seg %2")
                  .arg(summary.sources[0].wireTag).arg(summary.sources[0].segment)
            : countText(summary.sources.size(), tr("source"), tr("sources")));
    setSummary(ModelNode::Loads, countText(summary.loadCount, tr("load"), tr("loads")));
    setSummary(ModelNode::TransmissionLines,
        countText(summary.transmissionLineCount, tr("line"), tr("lines")));
    setSummary(ModelNode::Networks, summary.networkCount == 0 ? tr("none (NT)")
        : countText(summary.networkCount, tr("network (NT)"), tr("networks (NT)")));
    setSummary(ModelNode::Ground, groundName(summary.groundType));
    setSummary(ModelNode::Variables, summary.symbolCount == 0 ? tr("none (SY)")
        : countText(summary.symbolCount, tr("variable (SY)"), tr("variables (SY)")));
    setSummary(ModelNode::Requests, summary.radiationRequests.empty()
        ? summary.executionRequestCount == 0 ? tr("none") : tr("execution only")
        : summary.radiationRequests.size() == 1
            ? tr("pattern %1×%2").arg(summary.radiationRequests[0].thetaCount)
                  .arg(summary.radiationRequests[0].phiCount)
            : countText(summary.radiationRequests.size(), tr("pattern"), tr("patterns")));
    setSummary(ModelNode::Deck, countText(summary.cardCount, tr("card"), tr("cards")));

    // Rebuild the item rows, keeping what was selected and expanded.
    const auto selected = currentSelection();
    selecting_ = true;
    for (const auto node : {ModelNode::Wires, ModelNode::Sources, ModelNode::Loads,
             ModelNode::TransmissionLines, ModelNode::Networks})
        qDeleteAll(group(node)->takeChildren());
    for (const auto& wire : model.wires()) {
        addChild(group(ModelNode::Wires), {ModelNode::Wire, wire.tag}, tr("Wire %1").arg(wire.tag),
            tr("%1 seg · %2 m").arg(wire.segments).arg(formatDecimal(model::wireLength(wire))));
    }
    for (const auto& source : setup.excitations) {
        addChild(group(ModelNode::Sources), {ModelNode::Source, -1, source.sourceLine},
            tr("W%1 S%2").arg(source.wireTag).arg(source.segment),
            tr("%1 V ∠ %2°").arg(formatDecimal(source.magnitude),
                formatDecimal(source.phaseDegrees)));
    }
    for (const auto& load : setup.loads) {
        addChild(group(ModelNode::Loads), {ModelNode::Load, -1, load.sourceLine},
            load.firstSegment == load.lastSegment
                ? tr("W%1 S%2").arg(load.wireTag).arg(load.firstSegment)
                : tr("W%1 S%2–%3").arg(load.wireTag).arg(load.firstSegment).arg(load.lastSegment),
            loadTypeName(load.type));
    }
    for (const auto& line : setup.transmissionLines) {
        addChild(group(ModelNode::TransmissionLines),
            {ModelNode::TransmissionLine, -1, line.sourceLine},
            segmentPair(line.wireTag1, line.segment1, line.wireTag2, line.segment2),
            tr("Z0 %1 Ω · %2 m").arg(formatDecimal(line.characteristicImpedance),
                formatDecimal(line.lengthMeters)));
    }
    for (const auto& network : setup.networks) {
        addChild(group(ModelNode::Networks), {ModelNode::Network, -1, network.sourceLine},
            segmentPair(network.wireTag1, network.segment1, network.wireTag2, network.segment2),
            tr("two-port"));
    }
    auto* restored = selected ? itemFor(*selected) : nullptr;
    setCurrentItem(restored);
    if (restored == nullptr) clearSelection();
    selecting_ = false;
    updateGeometry();
}

void ModelTree::setSolver(const QString& summary)
{
    setSummary(ModelNode::Solver, summary);
}

void ModelTree::setStudyAction(Study study, QAction* action)
{
    auto* button = studyButtons_[static_cast<std::size_t>(study)];
    button->setToolTip(action->toolTip());
    button->setEnabled(action->isEnabled());
    connect(button, &QToolButton::clicked, action, &QAction::trigger);
    connect(action, &QAction::changed, button, [button, action] {
        button->setEnabled(action->isEnabled());
    });
}

void ModelTree::setStudyStatus(Study study, StudyState state, const QString& text,
    const QString& toolTip)
{
    const auto index = static_cast<std::size_t>(study);
    // Only a finished study can go stale; one not yet run stays "not run".
    if (state == StudyState::Stale && studyStates_[index] != StudyState::Complete) return;
    studyStates_[index] = state;
    const auto defaultText = [this, state] {
        switch (state) {
        case StudyState::NotRun: return tr("not run");
        case StudyState::Running: return tr("running");
        case StudyState::Complete: return tr("complete");
        case StudyState::Failed: return tr("failed");
        case StudyState::Stale: return tr("stale");
        }
        return QString{};
    }();
    studyLabels_[index]->setText(text.isEmpty() ? defaultText : text);
    studyLabels_[index]->setToolTip(toolTip.isEmpty() ? text : toolTip);
}

void ModelTree::clearStudy(Study study)
{
    setStudyStatus(study, StudyState::NotRun);
}

void ModelTree::select(const ModelSelection& selection)
{
    auto* item = itemFor(selection);
    selecting_ = true;
    setCurrentItem(item);
    if (item == nullptr) clearSelection();
    else scrollToItem(item);
    selecting_ = false;
}

auto ModelTree::currentSelection() const -> std::optional<ModelSelection>
{
    const auto* item = currentItem();
    if (item == nullptr || !item->isSelected()) return std::nullopt;
    return selectionOf(item);
}

void ModelTree::setSelectionChangingCallback(std::function<bool(const ModelSelection&)> callback)
{
    selectionChanging_ = std::move(callback);
}

void ModelTree::setSelectionChangedCallback(std::function<void(const ModelSelection&)> callback)
{
    selectionChanged_ = std::move(callback);
}

void ModelTree::setActivatedCallback(std::function<void(const ModelSelection&)> callback)
{
    activated_ = std::move(callback);
}

auto ModelTree::sizeHint() const -> QSize
{
    // As tall as the visible rows, so the properties pane gets the rest of the column.
    auto rows = 0;
    for (QTreeWidgetItemIterator it(const_cast<ModelTree*>(this)); *it != nullptr; ++it) {
        const auto* parent = (*it)->parent();
        if (parent == nullptr || parent->isExpanded()) ++rows;
    }
    const auto rowHeight = std::max(sizeHintForRow(0), fontMetrics().height() + 4);
    return {QTreeWidget::sizeHint().width(), rows * rowHeight + 2 * frameWidth() + 2};
}

}
