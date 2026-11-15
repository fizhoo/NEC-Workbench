#include "ui/modeltree/ModelColumn.h"

#include "ui/modeltree/ModelTree.h"
#include "ui/modeltree/PropertiesPane.h"
#include "ui/modeltree/VariablesSection.h"

#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto MinimumWidth = 360;
constexpr auto DefaultWidth = 460;
constexpr auto MaximumWidth = 600;
// With the pane open, the tree and the variables get at most these shares of the column.
constexpr auto TreeShare = 0.40;
constexpr auto VariablesShare = 0.25;
constexpr auto PaneTitleHeight = 28;

}

ModelColumn::ModelColumn(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("modelColumn"));
    setMinimumWidth(MinimumWidth);
    setMaximumWidth(MaximumWidth);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tree_ = new ModelTree(this);
    tree_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    variables_ = new VariablesSection(this);
    pane_ = new PropertiesPane(this);
    layout->addWidget(tree_);
    layout->addWidget(variables_);
    layout->addWidget(pane_, 1);
    // Takes the spare height when nothing else may: the variables folded and the pane empty.
    layout->addStretch(0);
    variables_->setFoldChangedCallback([this] { applyPolicies(); });
    limitHeights();
}

void ModelColumn::showPage(QWidget* page, const QString& title, bool table)
{
    table_ = page != nullptr && table;
    pane_->showPage(page, title);
    limitHeights();
}

auto ModelColumn::sizeHint() const -> QSize
{
    return {DefaultWidth, QWidget::sizeHint().height()};
}

void ModelColumn::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    limitHeights();
}

void ModelColumn::applyPolicies()
{
    // With the pane open the variables take their rows, up to a share, and scroll beyond
    // that; with it hidden they fill the rest of the column, unless folded to their header.
    const auto fills = pane_->isHidden() && !variables_->isFolded();
    variables_->setSizePolicy(QSizePolicy::Preferred,
        fills ? QSizePolicy::Expanding : QSizePolicy::Maximum);
    auto* column = static_cast<QVBoxLayout*>(layout());
    column->setStretchFactor(variables_, fills ? 1 : 0);
    column->setStretch(column->count() - 1, pane_->isHidden() && !fills ? 1 : 0);
}

void ModelColumn::limitHeights()
{
    const auto paneOpen = !pane_->isHidden();
    const auto rowHeight = tree_->sizeHintForRow(0);
    tree_->setMaximumHeight(paneOpen
        ? std::max(rowHeight * 4, static_cast<int>(height() * TreeShare)) : QWIDGETSIZE_MAX);
    applyPolicies();
    const auto variablesHeight = std::max(rowHeight * 3, static_cast<int>(height() * VariablesShare));
    variables_->setMaximumHeight(paneOpen ? variablesHeight : QWIDGETSIZE_MAX);
    // A form that would not fit below the tree and the variables folds the variables away.
    const auto treeHeight = std::min(tree_->sizeHint().height(), tree_->maximumHeight());
    const auto room = height() - treeHeight - std::min(variables_->unfoldedHeight(), variablesHeight);
    // The pane scrolls its page, so ask the page how tall it is; the title adds a line.
    const auto* page = pane_->currentPage();
    const auto needed = page == nullptr ? 0 : page->sizeHint().height() + PaneTitleHeight;
    variables_->setAutoFolded(paneOpen && (table_ || (height() > 0 && room < needed)));
}

}
