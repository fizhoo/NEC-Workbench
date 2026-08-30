#pragma once

#include "nec/NecDocument.h"

#include <QWidget>

#include <utility>
#include <vector>

class QLabel;
class QListWidget;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;

namespace necwb::ui {

class StructuredCardEditor final : public QWidget {
    Q_OBJECT

public:
    explicit StructuredCardEditor(QWidget* parent = nullptr);
    void setDocument(const nec::NecDocument& document);
    auto selectCard(std::size_t sourceLine) -> bool;

signals:
    void cardSelected(std::size_t sourceLine);
    void cardEdited(std::size_t sourceLine, QString cardText);
    void cardAddRequested(QString cardText);
    void cardDeleteRequested(std::size_t sourceLine);

private:
    void refreshFamilies();
    void refreshTable();
    void commitCell(QTableWidgetItem* item);
    void updateActions();
    [[nodiscard]] auto validateRow(int row) -> bool;
    [[nodiscard]] auto wireDefaults() const -> std::vector<std::pair<int, int>>;
    [[nodiscard]] auto defaultCard(int familyIndex) const -> QString;

    nec::NecDocument document_;
    QListWidget* families_{};
    QTableWidget* table_{};
    QLabel* description_{};
    QPushButton* addButton_{};
    QPushButton* deleteButton_{};
    bool updating_{};
};

}
