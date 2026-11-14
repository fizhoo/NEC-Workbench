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

class QLabel;
class QTimer;
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

    std::vector<nec::SymbolDefinition> variables_;
    // Ranges set here or first seen, for the session; other variables start afresh.
    mutable std::unordered_map<std::string, Range> ranges_;
    std::vector<std::unique_ptr<Row>> rows_;
    QWidget* rowsWidget_{};
    QVBoxLayout* rowsLayout_{};
    QLabel* empty_{};
    QTimer* stepTimer_{};
    Row* steppingRow_{};
    std::function<std::optional<Range>(const QString&)> rangeProvider_;
    std::function<void(const QString&, const Range&)> rangeChanged_;
    std::function<void(const nec::SymbolDefinition&, double)> valueChanged_;
};

}
