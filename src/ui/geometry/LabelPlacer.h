#pragma once

#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <functional>
#include <vector>

namespace necwb::ui {

// Places one paint's labels after everything else is drawn, so nothing paints over them.
// Each label sits beside its anchor on the side facing the middle of the view, whole inside
// the view and clear of the markers blocked and the labels placed before it, moving up or
// down, then to the other side, to find room.
class LabelPlacer {
public:
    using Draw = std::function<void(const QRectF&)>;

    void reset(const QRectF& bounds)
    {
        bounds_ = bounds;
        obstacles_.clear();
        placed_.clear();
        pending_.clear();
    }

    // Keeps labels off a marker or a wire end.
    void block(const QRectF& box) { obstacles_.push_back(box); }

    void add(const QPointF& anchor, const QSizeF& size, Draw draw)
    {
        pending_.push_back({anchor, size, std::move(draw)});
    }

    void placeAll()
    {
        for (const auto& label : pending_) {
            const auto box = place(label.anchor, label.size);
            placed_.push_back(box);
            label.draw(box);
        }
        pending_.clear();
    }

    [[nodiscard]] auto placed() const -> const std::vector<QRectF>& { return placed_; }

private:
    struct Pending {
        QPointF anchor;
        QSizeF size;
        Draw draw;
    };

    [[nodiscard]] auto place(const QPointF& anchor, const QSizeF& size) const -> QRectF
    {
        constexpr auto Gap = 8.0;
        constexpr auto Steps = 6;
        const auto inner = anchor.x() > bounds_.center().x() ? -1.0 : 1.0;
        // Above the anchor, or below it when there is no room above.
        const auto top = anchor.y() - Gap - size.height() >= bounds_.top()
            ? anchor.y() - Gap - size.height() / 2.0 : anchor.y() + Gap - size.height() / 2.0;
        const auto beside = [&](double side) {
            const auto left = side > 0.0 ? anchor.x() + Gap : anchor.x() - Gap - size.width();
            return QRectF(QPointF(left, top), size);
        };
        const auto step = size.height() + 1.0;
        for (const auto side : {inner, -inner}) {
            for (auto offset = 0; offset <= Steps; ++offset) {
                for (const auto direction : {-1.0, 1.0}) {
                    const auto box = beside(side).translated(0.0, direction * offset * step);
                    if (bounds_.contains(box) && isClear(box)) return box;
                    if (offset == 0) break;
                }
            }
        }
        return inside(beside(inner));
    }

    [[nodiscard]] auto inside(QRectF box) const -> QRectF
    {
        box.moveLeft(std::clamp(box.left(), bounds_.left(),
            std::max(bounds_.left(), bounds_.right() - box.width())));
        box.moveTop(std::clamp(box.top(), bounds_.top(),
            std::max(bounds_.top(), bounds_.bottom() - box.height())));
        return box;
    }

    [[nodiscard]] auto isClear(const QRectF& box) const -> bool
    {
        for (const auto& other : obstacles_)
            if (other.intersects(box)) return false;
        for (const auto& other : placed_)
            if (other.intersects(box)) return false;
        return true;
    }

    QRectF bounds_;
    std::vector<QRectF> obstacles_;
    std::vector<QRectF> placed_;
    std::vector<Pending> pending_;
};

}
