#include "ui/cards/StructuredCardEditor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole;
constexpr auto CardIndexRole = Qt::UserRole + 1;

struct CardFamily {
    QString title;
    QString description;
    std::vector<nec::NecCardKind> kinds;
    QStringList fields;
    QString mnemonic;
    bool singleton{};
};

const auto& families()
{
    static const std::array<CardFamily, 7> values{{
        {QObject::tr("Sources (EX)"), QObject::tr("Voltage and other excitation cards."),
            {nec::NecCardKind::Excitation},
            {QObject::tr("Type"), QObject::tr("Wire Tag"), QObject::tr("Segment"), QObject::tr("I4"),
                QObject::tr("Real"), QObject::tr("Imaginary"), QObject::tr("F7"), QObject::tr("F8"),
                QObject::tr("F9"), QObject::tr("F10")}, QStringLiteral("EX"), false},
        {QObject::tr("Frequency (FR)"), QObject::tr("Single-frequency and sweep definitions."),
            {nec::NecCardKind::Frequency},
            {QObject::tr("Mode"), QObject::tr("Count"), QObject::tr("I3"), QObject::tr("I4"),
                QObject::tr("Start MHz"), QObject::tr("Step/Ratio"), QObject::tr("F3"), QObject::tr("F4")},
            QStringLiteral("FR"), true},
        {QObject::tr("Ground (GN/GE)"), QObject::tr("Ground environment and geometry-end ground flag."),
            {nec::NecCardKind::Ground, nec::NecCardKind::GeometryEnd},
            {QObject::tr("Type/Flag"), QObject::tr("I2"), QObject::tr("I3"), QObject::tr("I4"),
                QObject::tr("Permittivity"), QObject::tr("Conductivity"), QObject::tr("F3"),
                QObject::tr("F4"), QObject::tr("F5"), QObject::tr("F6")}, QStringLiteral("GN"), true},
        {QObject::tr("Loads (LD)"), QObject::tr("Segment or wire loading definitions."),
            {nec::NecCardKind::Load},
            {QObject::tr("Type"), QObject::tr("Wire Tag"), QObject::tr("First Segment"),
                QObject::tr("Last Segment"), QObject::tr("Value 1"), QObject::tr("Value 2"),
                QObject::tr("Value 3")}, QStringLiteral("LD"), false},
        {QObject::tr("Transmission Lines (TL)"), QObject::tr("Two-port transmission-line connections."),
            {nec::NecCardKind::TransmissionLine},
            {QObject::tr("Wire 1"), QObject::tr("Segment 1"), QObject::tr("Wire 2"),
                QObject::tr("Segment 2"), QObject::tr("Z0"), QObject::tr("Length"),
                QObject::tr("Shunt R1"), QObject::tr("Shunt X1"), QObject::tr("Shunt R2"),
                QObject::tr("Shunt X2")}, QStringLiteral("TL"), false},
        {QObject::tr("Radiation Requests (RP)"), QObject::tr("Far-field sampling requests."),
            {nec::NecCardKind::RadiationPattern},
            {QObject::tr("Mode"), QObject::tr("Theta Count"), QObject::tr("Phi Count"),
                QObject::tr("Format"), QObject::tr("Theta Start"), QObject::tr("Phi Start"),
                QObject::tr("Theta Step"), QObject::tr("Phi Step"), QObject::tr("Distance"),
                QObject::tr("Normalization")}, QStringLiteral("RP"), false},
        {QObject::tr("Execution (XQ)"), QObject::tr("Calculation execution requests."),
            {nec::NecCardKind::Execute}, {QObject::tr("Option")}, QStringLiteral("XQ"), true},
    }};
    return values;
}

auto belongsTo(const nec::NecCard& card, const CardFamily& family) -> bool
{
    return std::ranges::find(family.kinds, card.kind) != family.kinds.end();
}

enum class FieldType { Integer, Number };

struct FieldChoice {
    QString value;
    QString label;
};

auto fieldType(const QString& mnemonic, int fieldIndex) -> FieldType
{
    if (mnemonic == QStringLiteral("GE") || mnemonic == QStringLiteral("XQ"))
        return FieldType::Integer;
    return fieldIndex < 4 ? FieldType::Integer : FieldType::Number;
}

auto choicesFor(const QString& mnemonic, int fieldIndex) -> std::vector<FieldChoice>
{
    if (fieldIndex != 0) return {};
    if (mnemonic == QStringLiteral("EX")) return {
        {QStringLiteral("0"), QObject::tr("0 — Applied voltage source")},
        {QStringLiteral("1"), QObject::tr("1 — Linear-polarized plane wave")},
        {QStringLiteral("2"), QObject::tr("2 — Right-hand elliptic plane wave")},
        {QStringLiteral("3"), QObject::tr("3 — Left-hand elliptic plane wave")},
        {QStringLiteral("4"), QObject::tr("4 — Elementary current source")},
        {QStringLiteral("5"), QObject::tr("5 — Current-slope voltage source")}};
    if (mnemonic == QStringLiteral("FR")) return {
        {QStringLiteral("0"), QObject::tr("0 — Linear frequency stepping")},
        {QStringLiteral("1"), QObject::tr("1 — Multiplicative frequency stepping")}};
    if (mnemonic == QStringLiteral("GN")) return {
        {QStringLiteral("-1"), QObject::tr("-1 — Free space / no ground")},
        {QStringLiteral("0"), QObject::tr("0 — Finite ground, reflection approximation")},
        {QStringLiteral("1"), QObject::tr("1 — Perfect conducting ground")},
        {QStringLiteral("2"), QObject::tr("2 — Finite ground, Sommerfeld/Norton")}};
    if (mnemonic == QStringLiteral("GE")) return {
        {QStringLiteral("0"), QObject::tr("0 — No ground-plane connection")},
        {QStringLiteral("1"), QObject::tr("1 — Geometry connected to ground")}};
    if (mnemonic == QStringLiteral("LD")) return {
        {QStringLiteral("-1"), QObject::tr("-1 — Clear all loads")},
        {QStringLiteral("0"), QObject::tr("0 — Series RLC")},
        {QStringLiteral("1"), QObject::tr("1 — Parallel RLC")},
        {QStringLiteral("2"), QObject::tr("2 — Distributed series RLC")},
        {QStringLiteral("3"), QObject::tr("3 — Distributed parallel RLC")},
        {QStringLiteral("4"), QObject::tr("4 — Fixed complex impedance")},
        {QStringLiteral("5"), QObject::tr("5 — Wire conductivity")}};
    return {};
}

auto choiceLabel(const QString& mnemonic, int fieldIndex, const QString& value) -> QString
{
    const auto choices = choicesFor(mnemonic, fieldIndex);
    const auto found = std::ranges::find(choices, value, &FieldChoice::value);
    return found == choices.end() ? value : found->label;
}

template<typename Value>
auto parsesAs(const QString& text) -> bool
{
    const auto value = text.trimmed().toStdString();
    Value parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return !value.empty() && error == std::errc{} && end == value.data() + value.size();
}

auto requiredFieldCount(const QString& mnemonic, const QStringList& fields) -> int
{
    if (mnemonic == QStringLiteral("EX") || mnemonic == QStringLiteral("FR")) return 6;
    if (mnemonic == QStringLiteral("GE") || mnemonic == QStringLiteral("XQ")) return 1;
    if (mnemonic == QStringLiteral("LD")) return 7;
    if (mnemonic == QStringLiteral("TL")) return 10;
    if (mnemonic == QStringLiteral("RP")) return 8;
    if (mnemonic == QStringLiteral("GN")) {
        const auto type = fields.empty() ? -1 : fields.front().toInt();
        return type == 0 || type == 2 ? 6 : 1;
    }
    return 0;
}

class CardFieldDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    auto createEditor(QWidget* parent, const QStyleOptionViewItem& option,
        const QModelIndex& index) const -> QWidget* override
    {
        if (index.column() < 2) return QStyledItemDelegate::createEditor(parent, option, index);
        const auto mnemonic = index.siblingAtColumn(1).data().toString();
        const auto choices = choicesFor(mnemonic, index.column()-2);
        if (!choices.empty()) {
            auto* editor = new QComboBox(parent);
            for (const auto& choice : choices) editor->addItem(choice.label, choice.value);
            return editor;
        }
        auto* editor = new QLineEdit(parent);
        if (fieldType(mnemonic, index.column()-2) == FieldType::Integer) {
            editor->setValidator(new QIntValidator(std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max(), editor));
        } else {
            auto* validator = new QDoubleValidator(editor);
            validator->setNotation(QDoubleValidator::ScientificNotation);
            editor->setValidator(validator);
        }
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            combo->setCurrentIndex(std::max(0, combo->findData(index.data().toString())));
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
        const QModelIndex& index) const override
    {
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            model->setData(index, combo->currentData().toString());
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        if (index.column() < 2) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const auto mnemonic = index.siblingAtColumn(1).data().toString();
        if (choicesFor(mnemonic, index.column()-2).empty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionComboBox combo;
        combo.rect = option.rect.adjusted(1, 1, -1, -1);
        combo.state = option.state | QStyle::State_Enabled;
        combo.palette = option.palette;
        combo.fontMetrics = option.fontMetrics;
        combo.currentText = choiceLabel(mnemonic, index.column()-2, index.data().toString());
        const auto* style = option.widget == nullptr ? QApplication::style() : option.widget->style();
        style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, option.widget);
        style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, option.widget);
    }

    auto sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const -> QSize override
    {
        auto result = QStyledItemDelegate::sizeHint(option, index);
        if (index.column() < 2) return result;
        const auto mnemonic = index.siblingAtColumn(1).data().toString();
        const auto choices = choicesFor(mnemonic, index.column()-2);
        if (choices.empty()) return result;
        auto width = 0;
        for (const auto& choice : choices)
            width = std::max(width, option.fontMetrics.horizontalAdvance(choice.label));
        result.setWidth(std::max(result.width(), width+34));
        return result;
    }
};

}

StructuredCardEditor::StructuredCardEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    description_ = new QLabel(this);
    description_->setWordWrap(true);
    auto* actions = new QHBoxLayout;
    addButton_ = new QPushButton(this);
    addButton_->setObjectName(QStringLiteral("structuredAddCardButton"));
    deleteButton_ = new QPushButton(tr("Delete Selected Card"), this);
    deleteButton_->setObjectName(QStringLiteral("structuredDeleteCardButton"));
    actions->addWidget(addButton_);
    actions->addWidget(deleteButton_);
    actions->addStretch();
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    families_ = new QListWidget(splitter);
    families_->setObjectName(QStringLiteral("structuredCardFamilies"));
    families_->setMinimumWidth(190);
    families_->setMaximumWidth(280);
    table_ = new QTableWidget(splitter);
    table_->setObjectName(QStringLiteral("structuredCardTable"));
    table_->setItemDelegate(new CardFieldDelegate(table_));
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(families_);
    splitter->addWidget(table_);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(description_);
    layout->addLayout(actions);
    layout->addWidget(splitter, 1);

    connect(families_, &QListWidget::currentRowChanged, this, [this] { refreshTable(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (updating_ || table_->currentRow() < 0) return;
        emit cardSelected(table_->item(table_->currentRow(), 0)->data(SourceLineRole).toULongLong());
        updateActions();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!updating_) commitCell(item);
    });
    connect(addButton_, &QPushButton::clicked, this, [this] {
        const auto familyIndex = families_->currentRow();
        if (familyIndex < 0) return;
        const auto cardText = defaultCard(familyIndex);
        if (cardText.isEmpty()) return;
        QTimer::singleShot(0, this, [this, cardText] { emit cardAddRequested(cardText); });
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this] {
        if (table_->currentRow() < 0 || table_->item(table_->currentRow(), 0) == nullptr) return;
        const auto sourceLine = table_->item(table_->currentRow(), 0)->data(SourceLineRole).toULongLong();
        QTimer::singleShot(0, this, [this, sourceLine] { emit cardDeleteRequested(sourceLine); });
    });
    refreshFamilies();
}

void StructuredCardEditor::setDocument(const nec::NecDocument& document)
{
    document_ = document;
    refreshFamilies();
    refreshTable();
}

auto StructuredCardEditor::selectCard(std::size_t sourceLine) -> bool
{
    const auto card = std::ranges::find(document_.cards(), sourceLine, &nec::NecCard::lineNumber);
    if (card == document_.cards().end()) return false;
    const auto family = std::ranges::find_if(families(), [&card](const auto& candidate) {
        return belongsTo(*card, candidate);
    });
    if (family == families().end()) return false;
    families_->setCurrentRow(static_cast<int>(std::distance(families().begin(), family)));
    for (auto row = 0; row < table_->rowCount(); ++row) {
        const auto* item = table_->item(row, 0);
        if (item != nullptr && item->data(SourceLineRole).toULongLong() == sourceLine) {
            table_->selectRow(row);
            table_->scrollToItem(item);
            return true;
        }
    }
    return false;
}

void StructuredCardEditor::refreshFamilies()
{
    const auto previous = families_->currentRow();
    const QSignalBlocker blocker(families_);
    families_->clear();
    for (const auto& family : families()) {
        const auto count = std::ranges::count_if(document_.cards(), [&family](const auto& card) {
            return belongsTo(card, family);
        });
        families_->addItem(QStringLiteral("%1 (%2)").arg(family.title).arg(count));
    }
    families_->setCurrentRow(previous >= 0 ? std::min(previous, families_->count()-1) : 0);
}

void StructuredCardEditor::refreshTable()
{
    const auto familyIndex = families_->currentRow();
    if (familyIndex < 0) return;
    const auto& family = families()[static_cast<std::size_t>(familyIndex)];
    updating_ = true;
    const QSignalBlocker blocker(table_);
    description_->setText(family.description + tr(" Double-click a field to edit its mapped source card."));
    auto fieldCount = family.fields.size();
    for (const auto& card : document_.cards()) {
        if (belongsTo(card, family))
            fieldCount = std::max(fieldCount, static_cast<qsizetype>(card.fields.size()));
    }
    auto headers = QStringList{tr("Line"), tr("Card")};
    headers.append(family.fields);
    while (headers.size() < fieldCount+2)
        headers.push_back(tr("Extra %1").arg(headers.size()-family.fields.size()-1));
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->setRowCount(0);
    const auto cards = document_.cards();
    for (std::size_t cardIndex = 0; cardIndex < cards.size(); ++cardIndex) {
        const auto& card = cards[cardIndex];
        if (!belongsTo(card, family)) continue;
        const auto row = table_->rowCount();
        table_->insertRow(row);
        auto* line = new QTableWidgetItem(QString::number(card.lineNumber));
        line->setFlags(line->flags() & ~Qt::ItemIsEditable);
        line->setData(SourceLineRole, static_cast<qulonglong>(card.lineNumber));
        line->setData(CardIndexRole, static_cast<qulonglong>(cardIndex));
        table_->setItem(row, 0, line);
        auto* mnemonic = new QTableWidgetItem(QString::fromStdString(card.mnemonic));
        mnemonic->setFlags(mnemonic->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, 1, mnemonic);
        for (auto column = 0; column < fieldCount; ++column) {
            const auto value = column < static_cast<int>(card.fields.size())
                ? QString::fromStdString(card.fields[static_cast<std::size_t>(column)]) : QString{};
            table_->setItem(row, column+2, new QTableWidgetItem(value));
        }
        static_cast<void>(validateRow(row));
    }
    table_->setColumnHidden(0, false);
    updating_ = false;
    updateActions();
}

void StructuredCardEditor::updateActions()
{
    const auto familyIndex = families_->currentRow();
    auto addEnabled = familyIndex >= 0;
    QString disabledReason;
    if (familyIndex >= 0) {
        const auto& family = families()[static_cast<std::size_t>(familyIndex)];
        addButton_->setText(tr("Add %1 Card").arg(family.mnemonic));
        const auto alreadyExists = std::ranges::any_of(document_.cards(), [&family](const auto& card) {
            return QString::fromStdString(card.mnemonic) == family.mnemonic;
        });
        if (family.singleton && alreadyExists) {
            addEnabled = false;
            disabledReason = tr("This model already has a %1 card. Select it in the table to edit it.")
                .arg(family.mnemonic);
        }
        if ((family.mnemonic == QStringLiteral("EX") || family.mnemonic == QStringLiteral("LD")
                || family.mnemonic == QStringLiteral("TL")) && wireDefaults().empty()) {
            addEnabled = false;
            disabledReason = tr("Add a GW wire before adding this card.");
        }
    }
    addButton_->setEnabled(addEnabled);
    addButton_->setToolTip(disabledReason);
    deleteButton_->setEnabled(table_->currentRow() >= 0 && !table_->selectedItems().empty());
}

void StructuredCardEditor::commitCell(QTableWidgetItem* item)
{
    if (item->column() < 2 || table_->item(item->row(), 0) == nullptr) return;
    const auto cardIndex = table_->item(item->row(), 0)->data(CardIndexRole).toULongLong();
    if (cardIndex >= document_.cards().size()) return;
    const auto& card = document_.cards()[cardIndex];
    if (!validateRow(item->row())) return;
    QStringList fields;
    for (auto column = 2; column < table_->columnCount(); ++column)
        fields.push_back(table_->item(item->row(), column)->text().trimmed());
    while (!fields.empty() && fields.back().isEmpty()) fields.removeLast();
    auto text = QString::fromStdString(card.mnemonic);
    for (const auto& field : fields) text += QLatin1Char(' ') + field;
    const auto sourceLine = card.lineNumber;
    QTimer::singleShot(0, this, [this, sourceLine, text = std::move(text)] {
        emit cardEdited(sourceLine, text);
    });
}

auto StructuredCardEditor::validateRow(int row) -> bool
{
    const auto* mnemonicItem = table_->item(row, 1);
    if (mnemonicItem == nullptr) return false;
    const auto mnemonic = mnemonicItem->text();
    QStringList fields;
    for (auto column = 2; column < table_->columnCount(); ++column)
        fields.push_back(table_->item(row, column)->text().trimmed());
    const auto required = requiredFieldCount(mnemonic, fields);
    auto valid = true;
    for (auto fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
        auto* cell = table_->item(row, fieldIndex+2);
        QString error;
        if (fields[fieldIndex].isEmpty()) {
            if (fieldIndex < required) error = tr("This field is required for %1.").arg(mnemonic);
        } else if (fieldType(mnemonic, fieldIndex) == FieldType::Integer
            && !parsesAs<int>(fields[fieldIndex])) {
            error = tr("Enter a whole number.");
        } else if (fieldType(mnemonic, fieldIndex) == FieldType::Number
            && !parsesAs<double>(fields[fieldIndex])) {
            error = tr("Enter a numeric value.");
        } else {
            const auto choices = choicesFor(mnemonic, fieldIndex);
            if (!choices.empty()
                && std::ranges::find(choices, fields[fieldIndex], &FieldChoice::value) == choices.end())
                error = tr("Choose a supported value from the dropdown.");
        }
        cell->setBackground(error.isEmpty() ? QBrush{} : QBrush{QColor(255, 205, 205)});
        cell->setToolTip(error.isEmpty() ? choiceLabel(mnemonic, fieldIndex, fields[fieldIndex]) : error);
        valid = valid && error.isEmpty();
    }
    return valid;
}

auto StructuredCardEditor::wireDefaults() const -> std::vector<std::pair<int, int>>
{
    std::vector<std::pair<int, int>> result;
    for (const auto& card : document_.cards()) {
        if (card.kind != nec::NecCardKind::GeometryWire || card.fields.size() < 2) continue;
        int tag{};
        int segments{};
        const auto tagText = QString::fromStdString(card.fields[0]);
        const auto segmentText = QString::fromStdString(card.fields[1]);
        if (parsesAs<int>(tagText) && parsesAs<int>(segmentText)) {
            tag = tagText.toInt();
            segments = segmentText.toInt();
            result.emplace_back(tag, std::max(1, segments));
        }
    }
    return result;
}

auto StructuredCardEditor::defaultCard(int familyIndex) const -> QString
{
    if (familyIndex < 0 || familyIndex >= static_cast<int>(families().size())) return {};
    const auto& family = families()[static_cast<std::size_t>(familyIndex)];
    const auto wires = wireDefaults();
    if (family.mnemonic == QStringLiteral("EX") && !wires.empty())
        return QStringLiteral("EX 0 %1 %2 0 1 0").arg(wires.front().first)
            .arg((wires.front().second+1)/2);
    if (family.mnemonic == QStringLiteral("FR")) return QStringLiteral("FR 0 1 0 0 14.175 0");
    if (family.mnemonic == QStringLiteral("GN")) {
        const auto grounded = std::ranges::any_of(document_.cards(), [](const auto& card) {
            return card.kind == nec::NecCardKind::GeometryEnd && !card.fields.empty()
                && card.fields.front() == "1";
        });
        return grounded ? QStringLiteral("GN 2 0 0 0 13 0.005") : QStringLiteral("GN -1");
    }
    if (family.mnemonic == QStringLiteral("LD") && !wires.empty())
        return QStringLiteral("LD 0 %1 1 %2 0 0 0").arg(wires.front().first).arg(wires.front().second);
    if (family.mnemonic == QStringLiteral("TL") && !wires.empty()) {
        const auto& second = wires.size() > 1 ? wires[1] : wires.front();
        return QStringLiteral("TL %1 %2 %3 %4 50 0 0 0 0 0")
            .arg(wires.front().first).arg((wires.front().second+1)/2)
            .arg(second.first).arg((second.second+1)/2);
    }
    if (family.mnemonic == QStringLiteral("RP")) return QStringLiteral("RP 0 37 1 1000 0 0 5 0 0 0");
    if (family.mnemonic == QStringLiteral("XQ")) return QStringLiteral("XQ 0");
    return {};
}

}
