#pragma once

#include "nec/NecDocument.h"
#include "nec/NecSymbolResolver.h"
#include "model/SegmentSharing.h"

#include <QStringList>
#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace necwb::ui {

class ElidedLabel;
class SymbolCells;

class StructuredCardEditor final : public QWidget {
    Q_OBJECT

public:
    // The card families shown: those named, or with exclude, all but those named. A single
    // family shows without the list of families. Names are mnemonics such as "LD".
    struct Families {
        QStringList names;
        bool exclude{};
    };

    explicit StructuredCardEditor(Families families, QWidget* parent = nullptr);
    // `resolved` gives the values of fields written as expressions; without it they show as written.
    void setDocument(const nec::NecDocument& document, const nec::NecDocument* resolved = nullptr);
    void setDeckUnitLabel(QString unitLabel);
    // The symbols a number cell offers as you type.
    void setSymbols(const std::vector<nec::SymbolDefinition>& symbols);
    // The model's shared segments, which the line under the table explains for a selected card.
    void setSharedSegments(std::vector<model::SharedSegment> shared);
    // Symbols, Make Variable… and expressions typed into a cell.
    [[nodiscard]] auto symbolCells() const -> SymbolCells* { return symbolCells_; }
    auto selectCard(std::size_t sourceLine) -> bool;
    void focusParameterizableField();
    // The card Add would add for the selected family.
    [[nodiscard]] auto newCard() const -> QString;

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
    // The headers and the line under the table, for the selected card.
    void showSelectedCard();
    [[nodiscard]] auto selectedCard() const -> const nec::NecCard*;
    [[nodiscard]] auto validateRow(int row) -> bool;
    [[nodiscard]] auto wireDefaults() const -> std::vector<std::pair<int, int>>;
    [[nodiscard]] auto defaultCard(int familyIndex) const -> QString;
    [[nodiscard]] auto currentFamilyIndex() const -> int;
    [[nodiscard]] auto familyItem(int familyIndex) const -> QTreeWidgetItem*;
    [[nodiscard]] auto shows(int familyIndex) const -> bool;

    nec::NecDocument document_;
    std::optional<nec::NecDocument> resolved_;
    SymbolCells* symbolCells_{};
    QTreeWidget* families_{};
    QTableWidget* table_{};
    Families shown_;
    QPushButton* addButton_{};
    QPushButton* deleteButton_{};
    ElidedLabel* note_{};
    // The family's own headers, which a selected card's type relabels.
    QStringList headers_;
    std::vector<model::SharedSegment> shared_;
    QString deckUnitLabel_{QStringLiteral("m")};
    bool updating_{};
};

}
