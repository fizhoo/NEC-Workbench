#pragma once

#include <QWidget>

namespace necwb::ui {

class ModelTree;
class PropertiesPane;
class VariablesSection;

// The main window's left column: the model tree, the variables with their sliders, and the
// properties pane for what is selected. The tree and the variables take the rows they show,
// up to a share of the column, and scroll beyond that; the pane takes the rest.
class ModelColumn final : public QWidget {
public:
    explicit ModelColumn(QWidget* parent = nullptr);

    [[nodiscard]] auto tree() const -> ModelTree* { return tree_; }
    [[nodiscard]] auto variables() const -> VariablesSection* { return variables_; }
    [[nodiscard]] auto pane() const -> PropertiesPane* { return pane_; }
    // Null hides the pane and gives the column to the tree.
    void showPage(QWidget* page, const QString& title);

    [[nodiscard]] auto sizeHint() const -> QSize override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void limitHeights();

    ModelTree* tree_{};
    VariablesSection* variables_{};
    PropertiesPane* pane_{};
};

}
