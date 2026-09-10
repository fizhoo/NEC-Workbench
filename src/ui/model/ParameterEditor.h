#pragma once

#include "nec/NecSymbolResolver.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace necwb::ui {

class ParameterEditor final : public QWidget {
    Q_OBJECT

public:
    explicit ParameterEditor(QWidget* parent = nullptr);

    void setResolution(const nec::SymbolResolution& resolution);
    void selectParameter(std::size_t sourceLine, const QString& name = {});
    void showEditError(const QString& message);
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool;
    void discardPendingEdits();

signals:
    void parameterChanged(std::size_t sourceLine, QString originalName,
        QString name, QString expression);
    void parameterDeleteRequested(std::size_t sourceLine, QString name);
    void parameterSelected(std::size_t sourceLine);

private:
    void addDraft();
    void applySelected();
    void revertPendingEdits();
    void updatePendingState(int row);
    void updateRowEditability();
    void updateActions();
    [[nodiscard]] auto pendingInputIsValid() const -> bool;
    [[nodiscard]] auto pendingNameIsUnique() const -> bool;

    std::vector<nec::SymbolDefinition> definitions_;
    QTableWidget* table_{};
    QPushButton* addButton_{};
    QPushButton* applyButton_{};
    QPushButton* revertButton_{};
    QPushButton* deleteButton_{};
    QLabel* statusLabel_{};
    int pendingRow_{-1};
    bool updating_{};
};

}
