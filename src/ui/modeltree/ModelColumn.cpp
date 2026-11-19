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
// The tree and, with the pane open, the variables get at most these shares of the column.
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
    variables_ = new VariablesSection(this);
    pane_ = new PropertiesPane(this);
    layout->addWidget(tree_);
    layout->addWidget(variables_);
    layout->addWidget(pane_, 1);
    // With the pane empty the spare height goes below the variables.
    layout->addStretch(1);
    tree_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    variables_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    fitTree();
}

void ModelColumn::fitTree()
{
    treeRows_ = tree_->fittedRows();
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

void ModelColumn::limitHeights()
{
    // The tree keeps the whole rows it was fitted to, up to its share, and scrolls.
    const auto frame = tree_->heightForRows(0);
    const auto rowHeight = std::max(1, tree_->heightForRows(1) - frame);
    const auto shareRows = static_cast<int>((height() * TreeShare - frame) / rowHeight);
    tree_->setFixedHeight(tree_->heightForRows(std::min(treeRows_, std::max(4, shareRows))));
    // The variables take their rows, up to a share with the pane open, and scroll beyond that.
    const auto paneOpen = !pane_->isHidden();
    const auto variablesHeight = std::max(rowHeight * 3, static_cast<int>(height() * VariablesShare));
    variables_->setMaximumHeight(paneOpen ? variablesHeight : QWIDGETSIZE_MAX);
    // The pane takes the spare height; with it empty, the space under the variables does.
    auto* column = static_cast<QVBoxLayout*>(layout());
    column->setStretch(column->count() - 1, paneOpen ? 0 : 1);
    // A page that would not fit below the tree and the variables folds the variables away.
    const auto room = height() - tree_->maximumHeight()
        - std::min(variables_->unfoldedHeight(), variablesHeight);
    // The pane scrolls its page, so ask the page how tall it is; the title adds a line.
    const auto* page = pane_->currentPage();
    const auto needed = page == nullptr ? 0 : page->sizeHint().height() + PaneTitleHeight;
    variables_->setAutoFolded(paneOpen && (table_ || (height() > 0 && room < needed)));
}

}
