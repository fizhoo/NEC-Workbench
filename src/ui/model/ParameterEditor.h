#pragma once

#include "nec/NecSymbolResolver.h"

#include <QWidget>

class QLabel;
class QLineEdit;
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

signals:
    void parameterChanged(std::size_t sourceLine, QString originalName,
        QString name, QString expression);
    void parameterDeleteRequested(std::size_t sourceLine, QString name);
    void parameterSelected(std::size_t sourceLine);

private:
    void loadSelection();
    void updateActions();
    [[nodiscard]] auto inputIsValid() const -> bool;

    std::vector<nec::SymbolDefinition> definitions_;
    QTableWidget* table_{};
    QLineEdit* nameControl_{};
    QLineEdit* expressionControl_{};
    QPushButton* addButton_{};
    QPushButton* updateButton_{};
    QPushButton* deleteButton_{};
    QLabel* statusLabel_{};
    bool updating_{};
};

}
