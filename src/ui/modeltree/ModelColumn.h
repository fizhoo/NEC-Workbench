#pragma once

#include <QWidget>

namespace necwb::ui {

class ModelTree;
class PropertiesPane;
class VariablesSection;

// The main window's left column: the model tree, the variables with their sliders, and the
// properties pane for what is selected. The tree takes its top-level rows and the studies,
// fitted when a model opens, and the variables the rows they show, each up to a share of the
// column, and scroll beyond that; the pane takes the rest, or with it empty, the space goes
// below the variables.
class ModelColumn final : public QWidget {
public:
    explicit ModelColumn(QWidget* parent = nullptr);

    [[nodiscard]] auto tree() const -> ModelTree* { return tree_; }
    [[nodiscard]] auto variables() const -> VariablesSection* { return variables_; }
    [[nodiscard]] auto pane() const -> PropertiesPane* { return pane_; }
    // Null hides the pane and gives the column to the tree. A table folds the variables
    // while it shows, as a page too tall for the column does.
    void showPage(QWidget* page, const QString& title, bool table = false);
    // Fits the tree to its top-level rows, for a model just opened, created or reloaded; it
    // keeps that height as rows are expanded, selected or added.
    void fitTree();

    [[nodiscard]] auto sizeHint() const -> QSize override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void limitHeights();

    ModelTree* tree_{};
    VariablesSection* variables_{};
    PropertiesPane* pane_{};
    bool table_{};
    int treeRows_{};
};

}
