#pragma once

#include <QRectF>

#include <algorithm>
#include <vector>

namespace necwb::ui {

// Places one paint's labels: each stays inside the bounds and, where it can, moves up or
// down clear of the labels placed before it.
class LabelPlacer {
public:
    void reset(const QRectF& bounds)
    {
        bounds_ = bounds;
        placed_.clear();
    }

    auto place(const QRectF& wanted) -> QRectF
    {
        constexpr auto Steps = 6;
        auto chosen = inside(wanted);
        for (auto step = 1; step <= Steps && !isClear(chosen); ++step) {
            for (const auto direction : {-1.0, 1.0}) {
                const auto moved = inside(wanted.translated(0.0, direction * step * (wanted.height() + 1.0)));
                if (!isClear(moved)) continue;
                chosen = moved;
                break;
            }
        }
        placed_.push_back(chosen);
        return chosen;
    }

    [[nodiscard]] auto placed() const -> const std::vector<QRectF>& { return placed_; }

private:
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
        return std::ranges::none_of(placed_, [&box](const QRectF& other) { return other.intersects(box); });
    }

    QRectF bounds_;
    std::vector<QRectF> placed_;
};

}
