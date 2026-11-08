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
    Variables,
    Requests,
    Solver,
    Deck,
    Validation,
    AverageGainTest,
    Convergence
};

// A row of the model tree: a group, or one wire, source, load, line or network in it.
struct ModelSelection {
    ModelNode node{ModelNode::Frequency};
    int wireTag{-1};
    std::size_t sourceLine{};

    auto operator==(const ModelSelection&) const -> bool = default;
};

// The model as a tree: each group with its one-line summary, expanding to its wires,
// sources, loads, lines and networks, and the model-quality studies with Run buttons.
class ModelTree final : public QTreeWidget {
public:
    enum class Study { AverageGain, Convergence };
    enum class StudyState { NotRun, Running, Complete, Failed, Stale };

    explicit ModelTree(QWidget* parent = nullptr);

    void setModel(const model::AntennaModel& model, const model::ModelSetup& setup,
        const nec::NecDocument& document);
    void setSolver(const QString& summary);
    void setStudyAction(Study study, QAction* action);
    void setStudyStatus(Study study, StudyState state, const QString& text = {},
        const QString& toolTip = {});
    void clearStudy(Study study);
    // Selects a row without reporting it; a missing row clears the selection.
    void select(const ModelSelection& selection);
    [[nodiscard]] auto currentSelection() const -> std::optional<ModelSelection>;
    // Called when the user selects a row; return false to stay on the previous row.
    void setSelectionChangingCallback(std::function<bool(const ModelSelection&)> callback);
    void setSelectionChangedCallback(std::function<void(const ModelSelection&)> callback);
    // Called when a row is clicked or activated, for rows that open something.
    void setActivatedCallback(std::function<void(const ModelSelection&)> callback);

    [[nodiscard]] auto sizeHint() const -> QSize override;

private:
    auto addGroup(ModelNode node, const QString& name, const char* icon) -> QTreeWidgetItem*;
    auto addChild(QTreeWidgetItem* group, ModelSelection selection, const QString& name,
        const QString& summary) -> QTreeWidgetItem*;
    [[nodiscard]] auto group(ModelNode node) const -> QTreeWidgetItem*;
    [[nodiscard]] auto itemFor(const ModelSelection& selection) const -> QTreeWidgetItem*;
    static auto selectionOf(const QTreeWidgetItem* item) -> ModelSelection;
    void setSummary(ModelNode node, const QString& summary);

    std::array<ElidedLabel*, 2> studyLabels_{};
    std::array<QToolButton*, 2> studyButtons_{};
    std::array<StudyState, 2> studyStates_{StudyState::NotRun, StudyState::NotRun};
    std::function<bool(const ModelSelection&)> selectionChanging_;
    std::function<void(const ModelSelection&)> selectionChanged_;
    std::function<void(const ModelSelection&)> activated_;
    bool selecting_{};
};

}
