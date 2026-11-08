#pragma once

#include <QLabel>
#include <QPainter>

namespace necwb::ui {

// A one-line label that ends in "…" when its text does not fit, so a long value cannot set
// the minimum width of the panel it sits in.
class ElidedLabel final : public QLabel {
public:
    using QLabel::QLabel;

    [[nodiscard]] auto minimumSizeHint() const -> QSize override
    {
        return {0, QLabel::minimumSizeHint().height()};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawText(contentsRect(), alignment() | Qt::TextSingleLine,
            fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

}
