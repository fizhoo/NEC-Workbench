#pragma once

#include <QLayout>
#include <QList>

namespace necwb::ui {

// Lays its widgets out left to right and starts a new line when the next one would not fit.
// A stretch added with addStretch() pushes what follows it to the right, on every line.
class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    void addStretch();
    [[nodiscard]] auto count() const -> int override;
    [[nodiscard]] auto itemAt(int index) const -> QLayoutItem* override;
    auto takeAt(int index) -> QLayoutItem* override;
    [[nodiscard]] auto expandingDirections() const -> Qt::Orientations override;
    [[nodiscard]] auto hasHeightForWidth() const -> bool override;
    [[nodiscard]] auto heightForWidth(int width) const -> int override;
    [[nodiscard]] auto minimumSize() const -> QSize override;
    [[nodiscard]] auto sizeHint() const -> QSize override;
    void setGeometry(const QRect& rect) override;

private:
    auto arrange(const QRect& rect, bool apply) const -> int;

    QList<QLayoutItem*> items_;
    int spacing_{};
};

}
