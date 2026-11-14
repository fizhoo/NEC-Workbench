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
    limitHeights();
}

void ModelColumn::showPage(QWidget* page, const QString& title)
{
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
    const auto paneOpen = !pane_->isHidden();
    const auto rowHeight = tree_->sizeHintForRow(0);
    tree_->setMaximumHeight(paneOpen
        ? std::max(rowHeight * 4, static_cast<int>(height() * TreeShare)) : QWIDGETSIZE_MAX);
    // With the pane open the variables take their rows, up to a share, and scroll beyond
    // that; with it hidden they fill the rest of the column.
    variables_->setSizePolicy(QSizePolicy::Preferred,
        paneOpen ? QSizePolicy::Maximum : QSizePolicy::Expanding);
    variables_->setMaximumHeight(paneOpen
        ? std::max(rowHeight * 3, static_cast<int>(height() * VariablesShare)) : QWIDGETSIZE_MAX);
}

}
