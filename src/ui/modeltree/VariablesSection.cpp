#include "ui/modeltree/VariablesSection.h"

#include "ui/DisplayFormat.h"
#include "ui/ElidedLabel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>

namespace necwb::ui {
namespace {

constexpr auto SliderSteps = 1000;
// Arrow-key and wheel steps this close together are one change.
constexpr auto StepPauseMs = 400;
constexpr auto DefaultSpan = 0.25;

// Shows six significant digits rather than a fixed count of decimals.
class ValueBox final : public QDoubleSpinBox {
public:
    explicit ValueBox(QWidget* parent) : QDoubleSpinBox(parent)
    {
        setDecimals(12);
        setRange(-1.0e12, 1.0e12);
        setKeyboardTracking(false);
        setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        // Twelve decimals would size the box for them; six significant digits fit this.
        setFixedWidth(72);
    }

protected:
    [[nodiscard]] auto textFromValue(double value) const -> QString override
    {
        return QString::number(value, 'g', 6);
    }
};

auto isPlainNumber(const std::string& expression) -> bool
{
    bool number{};
    QString::fromStdString(expression).trimmed().toDouble(&number);
    return number;
}

// The value the slider stands for, rounded to the slider's own step.
auto sliderValue(const VariablesSection::Range& range, int position) -> double
{
    const auto step = (range.second - range.first) / SliderSteps;
    const auto value = range.first + step * position;
    const auto decimals = std::max(0.0, -std::floor(std::log10(step)));
    const auto scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

}

struct VariablesSection::Row {
    nec::SymbolDefinition variable;
    Range range;
    QWidget* widget{};
    QSlider* slider{};
    QDoubleSpinBox* value{};
    ElidedLabel* formula{};
    bool updating{};
};

VariablesSection::VariablesSection(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("variablesSection"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(0);
    auto* heading = new QLabel(tr("VARIABLES"), this);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.85);
    heading->setFont(font);
    heading->setContentsMargins(6, 3, 6, 3);
    heading->setBackgroundRole(QPalette::AlternateBase);
    heading->setAutoFillBackground(true);
    heading->setToolTip(tr("Drag a slider or type a value to change the SY card. Results go "
        "stale; press F5 to run again."));
    empty_ = new QLabel(tr("No SY variables. Select Variables above to add one."), this);
    empty_->setForegroundRole(QPalette::PlaceholderText);
    empty_->setContentsMargins(6, 3, 6, 3);
    rowsWidget_ = new QWidget;
    rowsLayout_ = new QVBoxLayout(rowsWidget_);
    rowsLayout_->setContentsMargins(4, 2, 4, 2);
    rowsLayout_->setSpacing(1);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(rowsWidget_);
    layout->addWidget(heading);
    layout->addWidget(empty_);
    layout->addWidget(scroll, 1);
    stepTimer_ = new QTimer(this);
    stepTimer_->setSingleShot(true);
    stepTimer_->setInterval(StepPauseMs);
    connect(stepTimer_, &QTimer::timeout, this, [this] {
        if (steppingRow_ != nullptr) commit(*steppingRow_, steppingRow_->value->value());
    });
}

VariablesSection::~VariablesSection() = default;

void VariablesSection::setVariables(const std::vector<nec::SymbolDefinition>& variables)
{
    const auto sameNames = variables.size() == variables_.size()
        && std::ranges::equal(variables, variables_,
            [](const auto& left, const auto& right) { return left.name == right.name; });
    const auto shared = std::ranges::any_of(variables, [this](const auto& variable) {
        return std::ranges::any_of(variables_,
            [&variable](const auto& known) { return known.name == variable.name; });
    });
    if (!variables_.empty() && !shared) ranges_.clear();
    variables_ = variables;
    if (!sameNames) {
        rebuild();
        return;
    }
    // Same variables: update in place, so a field being edited keeps focus.
    for (auto index = std::size_t{0}; index < rows_.size(); ++index) {
        rows_[index]->variable = variables_[index];
        updateRow(*rows_[index]);
    }
}

void VariablesSection::setRangeProvider(
    std::function<std::optional<Range>(const QString&)> provider)
{
    rangeProvider_ = std::move(provider);
}

void VariablesSection::setRangeChangedCallback(
    std::function<void(const QString&, const Range&)> callback)
{
    rangeChanged_ = std::move(callback);
}

void VariablesSection::setValueChangedCallback(
    std::function<void(const nec::SymbolDefinition&, double)> callback)
{
    valueChanged_ = std::move(callback);
}

void VariablesSection::refreshRanges()
{
    for (auto& row : rows_) updateRow(*row);
}

auto VariablesSection::sizeHint() const -> QSize
{
    // As tall as the rows, so the properties pane gets the rest of the column.
    return {QWidget::sizeHint().width(), layout()->itemAt(0)->sizeHint().height()
        + (empty_->isVisible() ? empty_->sizeHint().height() : 0)
        + rowsWidget_->sizeHint().height() + 2};
}

auto VariablesSection::rangeOf(const nec::SymbolDefinition& variable) const -> Range
{
    if (rangeProvider_) {
        if (const auto range = rangeProvider_(QString::fromStdString(variable.name))) return *range;
    }
    if (const auto found = ranges_.find(variable.name); found != ranges_.end()) return found->second;
    // Anchored where the variable was first seen, so a drag does not move its own range.
    auto range = variable.value == 0.0 ? Range{-1.0, 1.0}
        : Range{variable.value * (1.0 - DefaultSpan), variable.value * (1.0 + DefaultSpan)};
    if (range.first > range.second) std::swap(range.first, range.second);
    ranges_.emplace(variable.name, range);
    return range;
}

void VariablesSection::rebuild()
{
    stepTimer_->stop();
    steppingRow_ = nullptr;
    rows_.clear();
    while (auto* item = rowsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto& variable : variables_) {
        auto row = std::make_unique<Row>();
        row->variable = variable;
        row->widget = new QWidget(rowsWidget_);
        auto* layout = new QHBoxLayout(row->widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* name = new ElidedLabel(QString::fromStdString(variable.name), row->widget);
        name->setObjectName(QStringLiteral("variableName"));
        name->setFixedWidth(fontMetrics().horizontalAdvance(QStringLiteral("wide_name_")));
        layout->addWidget(name);
        auto* raw = row.get();
        if (isPlainNumber(variable.expression)) {
            row->slider = new QSlider(Qt::Horizontal, row->widget);
            row->slider->setObjectName(QStringLiteral("variableSlider"));
            row->slider->setRange(0, SliderSteps);
            row->value = new ValueBox(row->widget);
            row->value->setObjectName(QStringLiteral("variableValue"));
            auto* rangeButton = new QToolButton(row->widget);
            rangeButton->setObjectName(QStringLiteral("variableRange"));
            rangeButton->setText(QStringLiteral("⋯"));
            rangeButton->setAutoRaise(true);
            rangeButton->setToolTip(tr("Set the slider's range"));
            layout->addWidget(row->slider, 1);
            layout->addWidget(row->value);
            layout->addWidget(rangeButton);
            connect(row->slider, &QSlider::valueChanged, this, [this, raw](int position) {
                if (raw->updating) return;
                const QSignalBlocker blocker(raw->value);
                raw->value->setValue(sliderValue(raw->range, position));
                // A drag commits on release; arrow keys and the wheel once they pause.
                if (raw->slider->isSliderDown()) return;
                steppingRow_ = raw;
                stepTimer_->start();
            });
            connect(row->slider, &QSlider::sliderReleased, this,
                [this, raw] { commit(*raw, raw->value->value()); });
            connect(row->value, &QDoubleSpinBox::editingFinished, this,
                [this, raw] { commit(*raw, raw->value->value()); });
            connect(rangeButton, &QToolButton::clicked, this, [this, raw] { editRange(*raw); });
        } else {
            row->formula = new ElidedLabel(row->widget);
            row->formula->setObjectName(QStringLiteral("variableFormula"));
            row->formula->setForegroundRole(QPalette::PlaceholderText);
            layout->addWidget(row->formula, 1);
        }
        rowsLayout_->addWidget(row->widget);
        updateRow(*row);
        rows_.push_back(std::move(row));
    }
    rowsLayout_->addStretch();
    empty_->setVisible(variables_.empty());
    updateGeometry();
}

void VariablesSection::updateRow(Row& row)
{
    const auto& variable = row.variable;
    const auto expression = QString::fromStdString(variable.expression);
    row.widget->setToolTip(tr("SY %1 = %2 (line %3)")
        .arg(QString::fromStdString(variable.name), expression).arg(variable.lineNumber));
    if (row.formula != nullptr) {
        row.formula->setText(tr("= %1 → %2").arg(expression, formatDecimal(variable.value)));
        return;
    }
    row.range = rangeOf(variable);
    row.updating = true;
    row.slider->setToolTip(tr("%1 to %2").arg(formatDecimal(row.range.first),
        formatDecimal(row.range.second)));
    const auto fraction = (variable.value - row.range.first) / (row.range.second - row.range.first);
    row.slider->setValue(std::clamp(static_cast<int>(std::lround(fraction * SliderSteps)), 0,
        SliderSteps));
    if (!row.value->hasFocus()) row.value->setValue(variable.value);
    row.updating = false;
}

void VariablesSection::editRange(Row& row)
{
    QMenu menu(this);
    auto* form = new QWidget(&menu);
    auto* layout = new QFormLayout(form);
    layout->setContentsMargins(8, 6, 8, 6);
    auto* minimum = new ValueBox(form);
    auto* maximum = new ValueBox(form);
    minimum->setObjectName(QStringLiteral("variableRangeMinimum"));
    maximum->setObjectName(QStringLiteral("variableRangeMaximum"));
    minimum->setValue(row.range.first);
    maximum->setValue(row.range.second);
    auto* set = new QPushButton(tr("Set Range"), form);
    set->setObjectName(QStringLiteral("variableRangeSet"));
    layout->addRow(tr("Minimum"), minimum);
    layout->addRow(tr("Maximum"), maximum);
    layout->addRow(set);
    auto* action = new QWidgetAction(&menu);
    action->setDefaultWidget(form);
    menu.addAction(action);
    connect(set, &QPushButton::clicked, &menu, [&] {
        if (minimum->value() >= maximum->value()) return;
        const Range range{minimum->value(), maximum->value()};
        ranges_[row.variable.name] = range;
        if (rangeChanged_) rangeChanged_(QString::fromStdString(row.variable.name), range);
        updateRow(row);
        menu.close();
    });
    menu.exec(row.widget->mapToGlobal(QPoint(row.widget->width(), row.widget->height())));
}

void VariablesSection::commit(Row& row, double value)
{
    stepTimer_->stop();
    steppingRow_ = nullptr;
    if (std::abs(value - row.variable.value) <= 1.0e-12 * std::max(1.0, std::abs(value))) return;
    // The change rebuilds the model, which updates these rows; let this handler finish first.
    QMetaObject::invokeMethod(this, [this, variable = row.variable, value] {
        if (valueChanged_) valueChanged_(variable, value);
    }, Qt::QueuedConnection);
}

}
