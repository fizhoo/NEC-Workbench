#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QStackedWidget;

namespace necwb::ui {

// Shows the editor for what is selected in the model tree. Pages are the existing editors;
// their Apply buttons mark unapplied edits, which Enter applies and Esc reverts.
class PropertiesPane final : public QWidget {
public:
    explicit PropertiesPane(QWidget* parent = nullptr);

    // Null hides the pane.
    void showPage(QWidget* page, const QString& title);
    [[nodiscard]] auto currentPage() const -> QWidget*;
    // Applies the current page's unapplied edits. When they cannot be applied, asks whether
    // to discard them; false means stay on the page.
    [[nodiscard]] auto commitPendingEdits() -> bool;
    void applyPendingEdits();
    // Puts back edits not yet applied, as Esc does; false when there are none.
    auto revertPendingEdits() -> bool;
    // Called to throw away a page's unapplied edits.
    void setDiscardCallback(std::function<void(QWidget*)> callback);
    void focusPage();
    // Shows ⇔ in the header, which widens the column to the page and back.
    void setExpandable(bool expandable);
    void setExpanded(bool expanded);
    [[nodiscard]] auto isExpanded() const -> bool;
    void setExpandCallback(std::function<void(bool)> callback);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] auto pendingButtons() const -> QList<QPushButton*>;

    QLabel* title_{};
    QToolButton* expand_{};
    QScrollArea* scroll_{};
    QStackedWidget* pages_{};
    std::function<void(QWidget*)> discard_;
    std::function<void(bool)> expanded_;
};

}
