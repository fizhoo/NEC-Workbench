#include "ui/modeltree/ModelColumn.h"

#include "ui/modeltree/ModelTree.h"
#include "ui/modeltree/PropertiesPane.h"

#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto MinimumWidth = 360;
constexpr auto DefaultWidth = 460;
constexpr auto MaximumWidth = 600;
// With the pane open, the tree gets at most this share of the column.
constexpr auto TreeShare = 0.45;

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
    pane_ = new PropertiesPane(this);
    layout->addWidget(tree_);
    layout->addWidget(pane_, 1);
    // Takes the space when the pane is hidden, so the tree keeps its row height.
    layout->addStretch(0);
}

void ModelColumn::showPage(QWidget* page, const QString& title)
{
    pane_->showPage(page, title);
    limitTreeHeight();
}

auto ModelColumn::sizeHint() const -> QSize
{
    return {DefaultWidth, QWidget::sizeHint().height()};
}

void ModelColumn::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    limitTreeHeight();
}

void ModelColumn::limitTreeHeight()
{
    tree_->setMaximumHeight(pane_->isHidden() ? QWIDGETSIZE_MAX
        : std::max(tree_->sizeHintForRow(0) * 4, static_cast<int>(height() * TreeShare)));
}

}
