#pragma once

#include "model/AntennaModel.h"
#include "model/ModelSetup.h"

#include <QTreeWidget>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

class QAction;
class QToolButton;

namespace necwb::nec { class NecDocument; }

namespace necwb::ui {

class ElidedLabel;

enum class ModelNode {
    Frequency,
    Wires,
    Wire,
    Sources,
    Source,
    Loads,
    Load,
    TransmissionLines,
    TransmissionLine,
    Networks,
    Network,
    Ground,
    Requests,
    ReferenceImpedance,
    // Cards with no group of their own: arcs, helices, patches, GS, GN, FR, RP and the rest.
    OtherCards,
    Variables,
    Deck,
    Validation,
    AverageGainTest,
    Convergence,
    // Not a row: the solver page, from the toolbar's engine list.
    Solver
};

// A row of the model tree: a group, or one wire, source, load, line or network in it.
struct ModelSelection {
    ModelNode node{ModelNode::Wires};
    int wireTag{-1};
    std::size_t sourceLine{};

    auto operator==(const ModelSelection&) const -> bool = default;
};

// The model as a tree: the frequency, ground, requests and reference impedance with their
// values, each group with its count (details in its tooltip; an empty group is one dim line),
// expanding to its wires, sources, loads, lines and networks, and the model-quality studies
// with Run buttons. Right-clicking a card group or its rows offers Add.
class ModelTree final : public QTreeWidget {
public:
    enum class Study { AverageGain, Convergence };
    enum class StudyState { NotRun, Running, Complete, Failed, Stale };

    explicit ModelTree(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const nec::NecDocument& document);
    // The study's Run button follows action's enabled state and tooltip and calls run.
    void setStudyAction(Study study, QAction* action, std::function<void()> run);
    void setStudyStatus(Study study, StudyState state, const QString& text = {},
        const QString& toolTip = {});
    void clearStudy(Study study);
    // Selects a row without reporting it; a missing row clears the selection.
    void select(const ModelSelection& selection);
    [[nodiscard]] auto currentSelection() const -> std::optional<ModelSelection>;
    // The row of the source, load, line or network on that deck line, if there is one.
    [[nodiscard]] auto selectionOfCard(std::size_t sourceLine) const -> std::optional<ModelSelection>;
    // Called when the user selects a row; return false to stay on the previous row.
    void setSelectionChangingCallback(std::function<bool(const ModelSelection&)> callback);
    void setSelectionChangedCallback(std::function<void(const ModelSelection&)> callback);
    // Called when a row is clicked or activated, for rows that open something.
    void setActivatedCallback(std::function<void(const ModelSelection&)> callback);
    // Called with the group to add to: Wires, Sources, Loads, TransmissionLines or Networks.
    void setAddCallback(std::function<void(ModelNode)> callback);

    // The rows showing with every card group collapsed: the top-level rows and the studies
    // under Validation. The column sizes the tree to them when a model opens, so expanding,
    // selecting or adding rows scrolls instead.
    [[nodiscard]] auto fittedRows() const -> int;
    // The tree's height for that many whole rows.
    [[nodiscard]] auto heightForRows(int rows) const -> int;

private:
    auto addGroup(ModelNode node, const QString& name, const char* icon) -> QTreeWidgetItem*;
    auto addChild(QTreeWidgetItem* group, ModelSelection selection, const QString& name,
        const QString& summary) -> QTreeWidgetItem*;
    [[nodiscard]] auto group(ModelNode node) const -> QTreeWidgetItem*;
    [[nodiscard]] auto itemFor(const ModelSelection& selection) const -> QTreeWidgetItem*;
    static auto selectionOf(const QTreeWidgetItem* item) -> ModelSelection;
    void setCount(ModelNode node, std::size_t count, const QString& detail);
    void setValue(ModelNode node, const QString& value, const QString& detail);
    void showAddMenu(const QPoint& position);

    std::array<ElidedLabel*, 2> studyLabels_{};
    std::array<QToolButton*, 2> studyButtons_{};
    std::array<StudyState, 2> studyStates_{StudyState::NotRun, StudyState::NotRun};
    std::function<bool(const ModelSelection&)> selectionChanging_;
    std::function<void(const ModelSelection&)> selectionChanged_;
    std::function<void(const ModelSelection&)> activated_;
    std::function<void(ModelNode)> add_;
    bool selecting_{};
};

}
