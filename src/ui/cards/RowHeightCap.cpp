#include "ui/cards/RowHeightCap.h"

#include <QEvent>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableView>
#include <QTimer>
#include <QTreeView>

namespace necwb::ui {

auto rowsHeight(const QAbstractItemView* view) -> int
{
    auto height = 2 * view->frameWidth();
    if (const auto* table = qobject_cast<const QTableView*>(view)) {
        if (!table->horizontalHeader()->isHidden())
            height += table->horizontalHeader()->sizeHint().height();
        height += table->verticalHeader()->length();
    } else if (const auto* tree = qobject_cast<const QTreeView*>(view)) {
        if (!tree->header()->isHidden()) height += tree->header()->sizeHint().height();
        for (auto index = tree->model()->index(0, 0, tree->rootIndex()); index.isValid();
             index = tree->indexBelow(index))
            height += tree->visualRect(index).height();
    }
    const auto* scrollBar = view->horizontalScrollBar();
    if (scrollBar->maximum() > scrollBar->minimum()) height += scrollBar->sizeHint().height();
    return height;
}

RowHeightCap::RowHeightCap(QWidget* capped, QAbstractItemView* view, std::function<int()> height)
    : QObject(capped), capped_(capped), height_(std::move(height))
{
    if (!height_) height_ = [view] { return rowsHeight(view); };
    watch(view);
    refresh();
}

void RowHeightCap::watch(QAbstractItemView* view)
{
    // Rows and their heights change in many ways; a queued refresh after each settles once.
    const auto later = [this] { QTimer::singleShot(0, this, &RowHeightCap::refresh); };
    connect(view->model(), &QAbstractItemModel::rowsInserted, this, later);
    connect(view->model(), &QAbstractItemModel::rowsRemoved, this, later);
    connect(view->model(), &QAbstractItemModel::modelReset, this, later);
    connect(view->model(), &QAbstractItemModel::layoutChanged, this, later);
    connect(view->horizontalScrollBar(), &QScrollBar::rangeChanged, this, later);
    if (auto* tree = qobject_cast<QTreeView*>(view)) {
        connect(tree, &QTreeView::expanded, this, later);
        connect(tree, &QTreeView::collapsed, this, later);
    }
    view->installEventFilter(this);
}

void RowHeightCap::refresh()
{
    const auto height = height_();
    if (capped_->maximumHeight() != height) capped_->setMaximumHeight(height);
}

auto RowHeightCap::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (event->type() == QEvent::Show || event->type() == QEvent::FontChange
        || event->type() == QEvent::StyleChange)
        QTimer::singleShot(0, this, &RowHeightCap::refresh);
    return QObject::eventFilter(watched, event);
}

}
