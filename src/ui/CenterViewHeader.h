#pragma once

#include <QWidget>

#include <functional>

namespace necwb::ui {

class ElidedLabel;

// The bar across the top of a view shown in the center in place of the geometry: a
// "← Geometry" button, the view's title, and room for a control on the right. Esc goes back
// too, while the view has focus.
class CenterViewHeader final : public QWidget {
public:
    // The view is the header's parent; objectName prefixes the back button and title names.
    CenterViewHeader(const QString& objectName, QWidget* view);

    void setTitle(const QString& title);
    void addTrailingWidget(QWidget* widget);
    void setBackCallback(std::function<void()> callback);

private:
    ElidedLabel* title_{};
    std::function<void()> back_;
};

// A view shown in the center in place of the geometry: the header over its content.
class CenterView final : public QWidget {
public:
    // objectName names the view and prefixes its header's back button and title.
    CenterView(const QString& objectName, QWidget* content, QWidget* parent = nullptr);

    [[nodiscard]] auto header() const -> CenterViewHeader* { return header_; }

private:
    CenterViewHeader* header_{};
};

}
