#pragma once

#include <QWidget>

namespace necwb::ui {

class ModelTree;
class PropertiesPane;

// The main window's left column: the model tree, and under it the properties pane for what
// is selected. The tree takes the rows it shows, up to a share of the column, and scrolls
// beyond that; the pane takes the rest and scrolls inside.
class ModelColumn final : public QWidget {
public:
    explicit ModelColumn(QWidget* parent = nullptr);

    [[nodiscard]] auto tree() const -> ModelTree* { return tree_; }
    [[nodiscard]] auto pane() const -> PropertiesPane* { return pane_; }
    // Null hides the pane and gives the column to the tree.
    void showPage(QWidget* page, const QString& title);

    [[nodiscard]] auto sizeHint() const -> QSize override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void limitTreeHeight();

    ModelTree* tree_{};
    PropertiesPane* pane_{};
};

}
