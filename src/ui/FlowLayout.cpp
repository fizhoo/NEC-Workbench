#include "ui/FlowLayout.h"

#include <QSpacerItem>
#include <QWidget>

#include <algorithm>

namespace necwb::ui {

FlowLayout::FlowLayout(QWidget* parent, int spacing) : QLayout(parent), spacing_(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (auto* item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem* item) { items_.append(item); }

void FlowLayout::addStretch()
{
    addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));
}

auto FlowLayout::count() const -> int { return static_cast<int>(items_.size()); }

auto FlowLayout::itemAt(int index) const -> QLayoutItem* { return items_.value(index); }

auto FlowLayout::takeAt(int index) -> QLayoutItem*
{
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

auto FlowLayout::expandingDirections() const -> Qt::Orientations { return {}; }

auto FlowLayout::hasHeightForWidth() const -> bool { return true; }

auto FlowLayout::heightForWidth(int width) const -> int { return arrange({0, 0, width, 0}, false); }

auto FlowLayout::minimumSize() const -> QSize
{
    // As narrow as the widest item; the height follows from heightForWidth.
    QSize size;
    for (const auto* item : items_)
        if (!item->isEmpty() && item->widget() != nullptr) size = size.expandedTo(item->minimumSize());
    const auto margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

auto FlowLayout::sizeHint() const -> QSize
{
    // Everything on one line.
    auto width = 0, height = 0;
    for (const auto* item : items_) {
        if (item->isEmpty() || item->widget() == nullptr) continue;
        width += item->sizeHint().width() + (width > 0 ? spacing_ : 0);
        height = std::max(height, item->sizeHint().height());
    }
    const auto margins = contentsMargins();
    return {width + margins.left() + margins.right(), height + margins.top() + margins.bottom()};
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

auto FlowLayout::arrange(const QRect& rect, bool apply) const -> int
{
    const auto margins = contentsMargins();
    const auto area = rect.marginsRemoved(margins);
    // A stretch pushes the items after it to the right end of each line they land on.
    struct Line { QList<QLayoutItem*> items; int stretchAt{-1}; int width{}; int height{}; };
    QList<Line> lines;
    auto stretch = false;
    for (auto* item : items_) {
        if (item->spacerItem() != nullptr) { stretch = true; continue; }
        if (item->isEmpty()) continue;
        const auto width = item->sizeHint().width();
        if (lines.isEmpty() || lines.back().width + spacing_ + width > area.width()) lines.append(Line{});
        auto& line = lines.back();
        if (stretch && line.stretchAt < 0) line.stretchAt = static_cast<int>(line.items.size());
        line.width += (line.items.isEmpty() ? 0 : spacing_) + width;
        line.height = std::max(line.height, item->sizeHint().height());
        line.items.append(item);
    }
    auto y = area.y();
    for (const auto& line : lines) {
        auto x = area.x();
        for (auto index = 0; index < line.items.size(); ++index) {
            if (index == line.stretchAt) {
                auto rest = -spacing_;
                for (auto after = index; after < line.items.size(); ++after)
                    rest += line.items[after]->sizeHint().width() + spacing_;
                x = std::max(x, area.x() + area.width() - rest);
            }
            const auto size = line.items[index]->sizeHint();
            if (apply)
                line.items[index]->setGeometry({QPoint(x, y + (line.height - size.height()) / 2), size});
            x += size.width() + spacing_;
        }
        y += line.height + spacing_;
    }
    const auto height = lines.isEmpty() ? 0 : y - spacing_ - area.y();
    return height + margins.top() + margins.bottom();
}

}
