#pragma once

#include "nec/NecSymbolResolver.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class QCheckBox;
class QLabel;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace necwb::ui {

// The model's SY variables, always shown under the model tree: a slider and a number box for
// each one written as a plain number, and the value of each formula. A drag, a typed value
// or a burst of arrow-key steps becomes one change of the SY card.
class VariablesSection final : public QWidget {
public:
    using Range = std::pair<double, double>;

    explicit VariablesSection(QWidget* parent = nullptr);
    ~VariablesSection() override;

    void setVariables(const std::vector<nec::SymbolDefinition>& variables);
    // A variable's own range, such as the optimizer's; without one a slider spans ±25%.
    void setRangeProvider(std::function<std::optional<Range>(const QString&)> provider);
    // Called when a range is edited here.
    void setRangeChangedCallback(std::function<void(const QString&, const Range&)> callback);
    // Called once per drag, typed value or burst of steps, with the variable's new value.
    void setValueChangedCallback(
        std::function<void(const nec::SymbolDefinition&, double)> callback);
    // Called on every move of a slider and every keystroke in a value, before the change.
    void setPreviewCallback(std::function<void(const nec::SymbolDefinition&, double)> callback);
    // Puts back a value typed but not yet applied; false when there is none.
    auto revertTyping() -> bool;

    // The variables as opened or saved, which Reset goes back to.
    void markBaseline();
    // Each variable whose SY expression differs from the baseline, with the baseline's.
    using Reset = std::vector<std::pair<nec::SymbolDefinition, std::string>>;
    void setResetCallback(std::function<void(const Reset&)> callback);
    // Whether a committed value is also solved at the selected frequency.
    [[nodiscard]] auto isLive() const -> bool;
    // Reads the ranges again, after the optimizer's change.
    void refreshRanges();

    [[nodiscard]] auto sizeHint() const -> QSize override;

private:
    struct Row;
    void rebuild();
    void updateRow(Row& row);
    [[nodiscard]] auto rangeOf(const nec::SymbolDefinition& variable) const -> Range;
    void editRange(Row& row);
    void commit(Row& row, double value);
    void preview(const Row& row, double value);
    [[nodiscard]] auto changesFromBaseline() const -> Reset;
    void updateReset();

    std::vector<nec::SymbolDefinition> variables_;
    // Ranges set here or first seen, for the session; other variables start afresh.
    mutable std::unordered_map<std::string, Range> ranges_;
    std::vector<std::unique_ptr<Row>> rows_;
    QWidget* rowsWidget_{};
    QVBoxLayout* rowsLayout_{};
    QLabel* empty_{};
    QCheckBox* live_{};
    QToolButton* reset_{};
    std::unordered_map<std::string, std::string> baseline_;
    QTimer* stepTimer_{};
    Row* steppingRow_{};
    std::function<std::optional<Range>(const QString&)> rangeProvider_;
    std::function<void(const QString&, const Range&)> rangeChanged_;
    std::function<void(const nec::SymbolDefinition&, double)> valueChanged_;
    std::function<void(const nec::SymbolDefinition&, double)> preview_;
    std::function<void(const Reset&)> resetRequested_;
};

}
