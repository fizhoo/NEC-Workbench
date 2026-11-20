#pragma once

#include <QObject>

#include <functional>

class QAbstractItemView;
class QWidget;

namespace necwb::ui {

// The height that shows every row of `view`, with its header, frame and any horizontal
// scroll bar, and no more.
[[nodiscard]] auto rowsHeight(const QAbstractItemView* view) -> int;

// Caps `capped` at the height its rows need, as rows come and go, so the line under a short
// table sits right under its last row with the spare height below. A table taller than the
// pane still shrinks and scrolls, leaving the line visible at the bottom.
class RowHeightCap final : public QObject {
public:
    // `height` gives the cap; it defaults to rowsHeight(view).
    RowHeightCap(QWidget* capped, QAbstractItemView* view, std::function<int()> height = {});

    void refresh();
    // Also refreshes when `view`'s rows change, for a cap that depends on more than one view.
    void watch(QAbstractItemView* view);

protected:
    auto eventFilter(QObject* watched, QEvent* event) -> bool override;

private:
    QWidget* capped_{};
    std::function<int()> height_;
};

}
