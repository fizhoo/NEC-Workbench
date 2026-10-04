#include "ui/cards/StructuredCardEditor.h"

#include "nec/NecCardCatalog.h"
#include "nec/NecCardFieldEditor.h"
#include "ui/ParameterFieldStyle.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace necwb::ui {
namespace {

constexpr auto SourceLineRole = Qt::UserRole;
constexpr auto CardIndexRole = Qt::UserRole + 1;
constexpr auto FamilyIndexRole = Qt::UserRole + 2;

struct CardFamily {
    QString category;
    QString title;
    QString description;
    std::vector<nec::NecCardKind> kinds;
    QStringList fields;
    QString mnemonic;
    bool singleton{};
};

const auto& families()
{
    static const std::array<CardFamily, 16> values{{
        {QObject::tr("Geometry"), QObject::tr("GA — Wire Arcs"),
            QObject::tr("Circular wire arcs in the XZ plane. Angles are measured in degrees; lengths use authored NEC deck units."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("Tag"), QObject::tr("Segments"), QObject::tr("Arc Radius (deck length)"),
                QObject::tr("Start Angle (°)"), QObject::tr("End Angle (°)"),
                QObject::tr("Wire Radius (deck length)")}, QStringLiteral("GA"), false},
        {QObject::tr("Geometry"), QObject::tr("GH — Helices and Spirals"),
            QObject::tr("Helical or spiral wire geometry. Endpoint radii may differ to create tapered elliptical forms; lengths use authored NEC deck units."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("Tag"), QObject::tr("Segments"), QObject::tr("Turn Spacing (deck length)"),
                QObject::tr("Axial Length (deck length)"), QObject::tr("Start X Radius (deck length)"),
                QObject::tr("Start Y Radius (deck length)"), QObject::tr("End X Radius (deck length)"),
                QObject::tr("End Y Radius (deck length)"), QObject::tr("Wire Radius (deck length)")},
            QStringLiteral("GH"), false},
        {QObject::tr("Geometry"), QObject::tr("SP — Surface Patches"),
            QObject::tr("Single arbitrary, rectangular, triangular, or quadrilateral surface patches. Shaped patches continue on an SC card."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("I1 (unused)"), QObject::tr("Shape"),
                QObject::tr("Center/Corner 1 X"), QObject::tr("Center/Corner 1 Y"),
                QObject::tr("Center/Corner 1 Z"), QObject::tr("Elevation/Corner 2 X"),
                QObject::tr("Azimuth/Corner 2 Y"), QObject::tr("Area/Corner 2 Z")},
            QStringLiteral("SP"), false},
        {QObject::tr("Geometry"), QObject::tr("SM — Patch Grids"),
            QObject::tr("Rectangular surfaces divided into a two-dimensional patch grid. A following SC card supplies corner 3."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("Patches U"), QObject::tr("Patches V"),
                QObject::tr("Corner 1 X"), QObject::tr("Corner 1 Y"), QObject::tr("Corner 1 Z"),
                QObject::tr("Corner 2 X"), QObject::tr("Corner 2 Y"), QObject::tr("Corner 2 Z")},
            QStringLiteral("SM"), false},
        {QObject::tr("Geometry"), QObject::tr("SC — Patch Continuations"),
            QObject::tr("Continuation coordinates for a preceding SP or SM surface definition."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("I1 (unused)"), QObject::tr("Next Shape"),
                QObject::tr("Corner 3 X"), QObject::tr("Corner 3 Y"), QObject::tr("Corner 3 Z"),
                QObject::tr("Corner 4 X"), QObject::tr("Corner 4 Y"), QObject::tr("Corner 4 Z")},
            QStringLiteral("SC"), false},
        {QObject::tr("Geometry"), QObject::tr("Other NEC-2 Geometry Cards"),
            QObject::tr("Remaining recognized geometry generators, patches, and transformations using NEC fixed fields."),
            {nec::NecCardKind::GeometryOther},
            {QObject::tr("I1"), QObject::tr("I2"), QObject::tr("F1"), QObject::tr("F2"),
                QObject::tr("F3"), QObject::tr("F4"), QObject::tr("F5"),
                QObject::tr("F6"), QObject::tr("F7")}, {}, false},
        {QObject::tr("Geometry"), QObject::tr("GS — Scale"),
            QObject::tr("Convert geometry coordinates and wire radii to meters for NEC."),
            {nec::NecCardKind::GeometryScale},
            {QObject::tr("I1 (unused)"), QObject::tr("I2 (unused)"),
                QObject::tr("Scale to meters")}, QStringLiteral("GS"), true},
        {QObject::tr("Environment"), QObject::tr("GN / GE — Ground"),
            QObject::tr("Ground environment and geometry-end ground flag."),
            {nec::NecCardKind::Ground, nec::NecCardKind::GeometryEnd},
            {QObject::tr("Type/Flag"), QObject::tr("I2"), QObject::tr("I3"), QObject::tr("I4"),
                QObject::tr("Permittivity"), QObject::tr("Conductivity"), QObject::tr("F3"),
                QObject::tr("F4"), QObject::tr("F5"), QObject::tr("F6")}, QStringLiteral("GN"), true},
        {QObject::tr("Sources"), QObject::tr("EX — Excitation"),
            QObject::tr("Voltage and other excitation cards."),
            {nec::NecCardKind::Excitation},
            {QObject::tr("Type"), QObject::tr("Wire Tag"), QObject::tr("Segment"), QObject::tr("I4"),
                QObject::tr("Real"), QObject::tr("Imaginary"), QObject::tr("F7"), QObject::tr("F8"),
                QObject::tr("F9"), QObject::tr("F10")}, QStringLiteral("EX"), false},
        {QObject::tr("Loads & Networks"), QObject::tr("LD — Loads"),
            QObject::tr("Segment or wire loading definitions."),
            {nec::NecCardKind::Load},
            {QObject::tr("Type"), QObject::tr("Wire Tag"), QObject::tr("First Segment"),
                QObject::tr("Last Segment"), QObject::tr("Value 1"), QObject::tr("Value 2"),
                QObject::tr("Value 3")}, QStringLiteral("LD"), false},
        {QObject::tr("Loads & Networks"), QObject::tr("TL — Transmission Lines"),
            QObject::tr("Two-port transmission-line connections."),
            {nec::NecCardKind::TransmissionLine},
            {QObject::tr("Wire 1"), QObject::tr("Segment 1"), QObject::tr("Wire 2"),
                QObject::tr("Segment 2"), QObject::tr("Z0"), QObject::tr("Length"),
                QObject::tr("Shunt R1"), QObject::tr("Shunt X1"), QObject::tr("Shunt R2"),
                QObject::tr("Shunt X2")}, QStringLiteral("TL"), false},
        {QObject::tr("Analysis & Requests"), QObject::tr("Other NEC-2 Control Cards"),
            QObject::tr("Recognized NEC-2 option, network, field-request, and output-control cards using NEC fixed fields."),
            {nec::NecCardKind::ControlOther, nec::NecCardKind::Network},
            {QObject::tr("I1"), QObject::tr("I2"), QObject::tr("I3"), QObject::tr("I4"),
                QObject::tr("F1"), QObject::tr("F2"), QObject::tr("F3"),
                QObject::tr("F4"), QObject::tr("F5"), QObject::tr("F6")}, {}, false},
        {QObject::tr("Analysis & Requests"), QObject::tr("FR — Frequency"),
            QObject::tr("Single-frequency and sweep definitions."),
            {nec::NecCardKind::Frequency},
            {QObject::tr("Mode"), QObject::tr("Count"), QObject::tr("I3"), QObject::tr("I4"),
                QObject::tr("Start MHz"), QObject::tr("Step/Ratio"), QObject::tr("F3"), QObject::tr("F4")},
            QStringLiteral("FR"), true},
        {QObject::tr("Analysis & Requests"), QObject::tr("RP — Radiation Pattern"),
            QObject::tr("Far-field sampling requests."),
            {nec::NecCardKind::RadiationPattern},
            {QObject::tr("Mode"), QObject::tr("Theta Count"), QObject::tr("Phi Count"),
                QObject::tr("Format"), QObject::tr("Theta Start"), QObject::tr("Phi Start"),
                QObject::tr("Theta Step"), QObject::tr("Phi Step"), QObject::tr("Distance"),
                QObject::tr("Normalization")}, QStringLiteral("RP"), false},
        {QObject::tr("Analysis & Requests"), QObject::tr("Z0 / ZO — Reference Impedance"),
            QObject::tr("xnec2c-compatible SWR reference impedance; omitted from standard nec2c solver input."),
            {nec::NecCardKind::ReferenceImpedance}, {QObject::tr("Reference Ohms")},
            QStringLiteral("Z0"), true},
        {QObject::tr("Program Control"), QObject::tr("XQ — Execute"),
            QObject::tr("Calculation execution requests."),
            {nec::NecCardKind::Execute}, {QObject::tr("Option")}, QStringLiteral("XQ"), true},
    }};
    return values;
}

auto belongsTo(const nec::NecCard& card, const CardFamily& family) -> bool
{
    const auto dedicatedGeometry = family.mnemonic == QStringLiteral("GA")
        || family.mnemonic == QStringLiteral("GH") || family.mnemonic == QStringLiteral("SP")
        || family.mnemonic == QStringLiteral("SM") || family.mnemonic == QStringLiteral("SC");
    if (dedicatedGeometry) return QString::fromStdString(card.mnemonic) == family.mnemonic;
    if (family.kinds.size() == 1 && family.kinds.front() == nec::NecCardKind::GeometryOther
        && (card.mnemonic == "GA" || card.mnemonic == "GH" || card.mnemonic == "SP"
            || card.mnemonic == "SM" || card.mnemonic == "SC")) return false;
    return std::ranges::find(family.kinds, card.kind) != family.kinds.end();
}

enum class FieldType { Integer, Number };

struct FieldChoice {
    QString value;
    QString label;
};

auto fieldType(const QString& mnemonic, int fieldIndex) -> FieldType
{
    if (const auto* spec = nec::findNecCardSpec(mnemonic.toStdString()))
        return static_cast<std::size_t>(fieldIndex) < spec->integerFieldCount
            ? FieldType::Integer : FieldType::Number;
    if (mnemonic == QStringLiteral("Z0") || mnemonic == QStringLiteral("ZO"))
        return FieldType::Number;
    if (mnemonic == QStringLiteral("GS"))
        return fieldIndex < 2 ? FieldType::Integer : FieldType::Number;
    if (mnemonic == QStringLiteral("GE") || mnemonic == QStringLiteral("XQ"))
        return FieldType::Integer;
    return fieldIndex < 4 ? FieldType::Integer : FieldType::Number;
}

auto choicesFor(const QString& mnemonic, int fieldIndex) -> std::vector<FieldChoice>
{
    if ((mnemonic == QStringLiteral("SP") || mnemonic == QStringLiteral("SC"))
        && fieldIndex == 1) return {
        {QStringLiteral("0"), QObject::tr("0 — Arbitrary patch / first continuation")},
        {QStringLiteral("1"), QObject::tr("1 — Rectangular patch")},
        {QStringLiteral("2"), QObject::tr("2 — Triangular patch")},
        {QStringLiteral("3"), QObject::tr("3 — Quadrilateral patch")}};
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
    if (mnemonic == QStringLiteral("GA")) return 6;
    if (mnemonic == QStringLiteral("GH")) return 9;
    if (mnemonic == QStringLiteral("SP") || mnemonic == QStringLiteral("SM")) return 8;
    if (mnemonic == QStringLiteral("SC"))
        return fields.size() > 1 && fields[1].toInt() == 3 ? 8 : 5;
    if (mnemonic == QStringLiteral("EX") || mnemonic == QStringLiteral("FR")) return 6;
    if (mnemonic == QStringLiteral("GS")) return 3;
    if (mnemonic == QStringLiteral("GE") || mnemonic == QStringLiteral("XQ")) return 1;
    if (mnemonic == QStringLiteral("Z0") || mnemonic == QStringLiteral("ZO")) return 1;
    if (mnemonic == QStringLiteral("LD")) return 7;
    if (mnemonic == QStringLiteral("TL")) return 10;
    if (mnemonic == QStringLiteral("RP")) return 8;
    if (mnemonic == QStringLiteral("GN")) {
        const auto type = fields.empty() ? -1 : fields.front().toInt();
        return type == 0 || type == 2 ? 6 : 1;
    }
    return 0;
}

auto fieldHelp(const QString& mnemonic, int fieldIndex) -> QString
{
    if (mnemonic == QStringLiteral("GA")) {
        static const std::array help{
            QObject::tr("Structure tag used by EX, LD, TL, and result references."),
            QObject::tr("Number of straight NEC segments used to approximate the arc."),
            QObject::tr("Arc radius in the authored NEC deck length unit."),
            QObject::tr("Arc starting angle in degrees in the XZ plane."),
            QObject::tr("Arc ending angle in degrees in the XZ plane."),
            QObject::tr("Physical wire radius in the authored NEC deck length unit.")};
        if (fieldIndex >= 0 && fieldIndex < static_cast<int>(help.size())) return help[fieldIndex];
    }
    if (mnemonic == QStringLiteral("GH")) {
        static const std::array help{
            QObject::tr("Structure tag used by EX, LD, TL, and result references."),
            QObject::tr("Number of NEC wire segments along the generated path."),
            QObject::tr("Axial spacing per turn; its sign controls winding direction."),
            QObject::tr("Total axial length. A zero value denotes NEC spiral form and is not yet rendered graphically."),
            QObject::tr("X radius at the beginning of the helix or spiral."),
            QObject::tr("Y radius at the beginning of the helix or spiral."),
            QObject::tr("X radius at the end of the helix or spiral."),
            QObject::tr("Y radius at the end of the helix or spiral."),
            QObject::tr("Physical wire radius in the authored NEC deck length unit.")};
        if (fieldIndex >= 0 && fieldIndex < static_cast<int>(help.size())) return help[fieldIndex];
    }
    if (mnemonic == QStringLiteral("SP")) {
        static const std::array help{
            QObject::tr("Must be zero for an SP card."),
            QObject::tr("0 arbitrary, 1 rectangular, 2 triangular, or 3 quadrilateral."),
            QObject::tr("Arbitrary-patch center X or shaped-patch corner 1 X."),
            QObject::tr("Arbitrary-patch center Y or shaped-patch corner 1 Y."),
            QObject::tr("Arbitrary-patch center Z or shaped-patch corner 1 Z."),
            QObject::tr("Normal elevation in degrees or shaped-patch corner 2 X."),
            QObject::tr("Normal azimuth in degrees or shaped-patch corner 2 Y."),
            QObject::tr("Positive patch area or shaped-patch corner 2 Z.")};
        if (fieldIndex >= 0 && fieldIndex < static_cast<int>(help.size())) return help[fieldIndex];
    }
    if (mnemonic == QStringLiteral("SM")) {
        static const std::array help{
            QObject::tr("Positive number of patches along the first surface direction."),
            QObject::tr("Positive number of patches along the second surface direction."),
            QObject::tr("First surface corner X."), QObject::tr("First surface corner Y."),
            QObject::tr("First surface corner Z."), QObject::tr("Second surface corner X."),
            QObject::tr("Second surface corner Y."), QObject::tr("Second surface corner Z.")};
        if (fieldIndex >= 0 && fieldIndex < static_cast<int>(help.size())) return help[fieldIndex];
    }
    if (mnemonic == QStringLiteral("SC")) {
        return QObject::tr("Continuation data for the immediately preceding SP or SM card. Corner 4 is required for quadrilateral patches.");
    }
    return {};
}

auto semanticFieldError(const QString& mnemonic, const QStringList& fields, int fieldIndex) -> QString
{
    if (mnemonic == QStringLiteral("GA") && fields.size() >= 6) {
        if (fieldIndex == 1 && fields[1].toInt() <= 0)
            return QObject::tr("Segment count must be greater than zero.");
        if ((fieldIndex == 2 || fieldIndex == 5) && fields[fieldIndex].toDouble() <= 0.0)
            return QObject::tr("Radius must be greater than zero.");
        if ((fieldIndex == 3 || fieldIndex == 4)
            && fields[3].toDouble() == fields[4].toDouble())
            return QObject::tr("Start and end angles must be different.");
    }
    if (mnemonic == QStringLiteral("GH") && fields.size() >= 9) {
        if (fieldIndex == 1 && fields[1].toInt() <= 0)
            return QObject::tr("Segment count must be greater than zero.");
        if (fieldIndex == 2 && fields[2].toDouble() == 0.0)
            return QObject::tr("Turn spacing must be nonzero.");
        if (fieldIndex >= 4 && fieldIndex <= 8 && fields[fieldIndex].toDouble() <= 0.0)
            return QObject::tr("Radius must be greater than zero.");
    }
    if (mnemonic == QStringLiteral("SP") && fields.size() >= 8) {
        if (fieldIndex == 0 && fields[0].toInt() != 0)
            return QObject::tr("SP I1 must be zero.");
        if (fieldIndex == 1 && (fields[1].toInt() < 0 || fields[1].toInt() > 3))
            return QObject::tr("Patch shape must be 0 through 3.");
        if (fields[1].toInt() == 0 && fieldIndex == 7 && fields[7].toDouble() <= 0.0)
            return QObject::tr("Arbitrary patch area must be greater than zero.");
    }
    if (mnemonic == QStringLiteral("SM") && fields.size() >= 2
        && fieldIndex < 2 && fields[fieldIndex].toInt() <= 0)
        return QObject::tr("Patch count must be greater than zero.");
    return {};
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
    families_ = new QTreeWidget(splitter);
    families_->setObjectName(QStringLiteral("structuredCardFamilies"));
    families_->setHeaderHidden(true);
    families_->setIndentation(14);
    families_->setMinimumWidth(190);
    families_->setMaximumWidth(280);
    table_ = new QTableWidget(splitter);
    table_->setObjectName(QStringLiteral("structuredCardTable"));
    table_->setItemDelegate(new CardFieldDelegate(table_));
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
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

    connect(families_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current) {
            if (current != nullptr && current->childCount() > 0
                && !current->data(0, FamilyIndexRole).isValid()) {
                families_->setCurrentItem(current->child(0));
                return;
            }
            refreshTable();
        });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (updating_ || table_->currentRow() < 0) return;
        emit cardSelected(table_->item(table_->currentRow(), 0)->data(SourceLineRole).toULongLong());
        updateActions();
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!updating_) commitCell(item);
    });
    connect(table_, &QTableWidget::customContextMenuRequested, this,
        [this](const QPoint& position) {
            auto* item = table_->itemAt(position);
            if (item == nullptr || item->column() < 2
                || table_->item(item->row(), 0) == nullptr) return;
            const auto mnemonic = table_->item(item->row(), 1)->text();
            const auto fieldIndex = item->column() - 2;
            if (fieldType(mnemonic, fieldIndex) != FieldType::Number
                || !choicesFor(mnemonic, fieldIndex).empty()) return;
            const auto sourceLine = table_->item(item->row(), 0)
                ->data(SourceLineRole).toULongLong();
            const auto parameterLine = parameterControlledFields_.find(sourceLine);
            const auto parameterControlled = parameterLine != parameterControlledFields_.end()
                && parameterLine->second.contains(static_cast<std::size_t>(fieldIndex));
            if (!parameterControlled && !parsesAs<double>(item->text())) return;
            QMenu menu(this);
            auto* parameterize = menu.addAction(parameterControlled
                ? tr("Change Parameter Link…") : tr("Parameterize Field…"));
            auto* detach = parameterControlled
                ? menu.addAction(tr("Replace With Current Numeric Value")) : nullptr;
            const auto* selected = menu.exec(table_->viewport()->mapToGlobal(position));
            const auto label = table_->horizontalHeaderItem(item->column())->text();
            if (selected == parameterize) {
                emit fieldParameterizationRequested(sourceLine,
                    static_cast<std::size_t>(fieldIndex), label);
            } else if (selected == detach) {
                emit fieldDetachmentRequested(sourceLine,
                    static_cast<std::size_t>(fieldIndex), label);
            }
        });
    connect(addButton_, &QPushButton::clicked, this, [this] {
        const auto familyIndex = currentFamilyIndex();
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

void StructuredCardEditor::setDeckUnitLabel(QString unitLabel)
{
    if (unitLabel.isEmpty() || unitLabel == deckUnitLabel_) return;
    deckUnitLabel_ = std::move(unitLabel);
    refreshTable();
}

void StructuredCardEditor::setParameterControlledFields(
    nec::NecParameterFieldMap sourceFields)
{
    parameterControlledFields_ = std::move(sourceFields);
}

void StructuredCardEditor::focusParameterizableField()
{
    for (auto row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0) == nullptr || table_->item(row, 1) == nullptr) continue;
        const auto mnemonic = table_->item(row, 1)->text();
        const auto sourceLine = table_->item(row, 0)->data(SourceLineRole).toULongLong();
        for (auto column = 2; column < table_->columnCount(); ++column) {
            auto* item = table_->item(row, column);
            if (item == nullptr) continue;
            const auto fieldIndex = column-2;
            if (fieldType(mnemonic, fieldIndex) != FieldType::Number
                || !choicesFor(mnemonic, fieldIndex).empty()) continue;
            const auto parameterLine = parameterControlledFields_.find(sourceLine);
            const auto parameterControlled = parameterLine != parameterControlledFields_.end()
                && parameterLine->second.contains(static_cast<std::size_t>(fieldIndex));
            if (!parameterControlled && !parsesAs<double>(item->text())) continue;
            table_->setCurrentCell(row, column);
            table_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            table_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    table_->setFocus(Qt::OtherFocusReason);
}

auto StructuredCardEditor::selectCard(std::size_t sourceLine) -> bool
{
    const auto card = std::ranges::find(document_.cards(), sourceLine, &nec::NecCard::lineNumber);
    if (card == document_.cards().end()) return false;
    const auto family = std::ranges::find_if(families(), [&card](const auto& candidate) {
        return belongsTo(*card, candidate);
    });
    if (family == families().end()) return false;
    families_->setCurrentItem(familyItem(static_cast<int>(std::distance(families().begin(), family))));
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
    const auto previous = currentFamilyIndex();
    const QSignalBlocker blocker(families_);
    families_->clear();
    for (std::size_t index = 0; index < families().size(); ++index) {
        const auto& family = families()[index];
        const auto count = std::ranges::count_if(document_.cards(), [&family](const auto& card) {
            return belongsTo(card, family);
        });
        QTreeWidgetItem* category{};
        for (auto top = 0; top < families_->topLevelItemCount(); ++top) {
            if (families_->topLevelItem(top)->text(0) == family.category) {
                category = families_->topLevelItem(top);
                break;
            }
        }
        if (category == nullptr) category = new QTreeWidgetItem(families_, {family.category});
        auto* item = new QTreeWidgetItem(category,
            {QStringLiteral("%1 (%2)").arg(family.title).arg(count)});
        item->setData(0, FamilyIndexRole, static_cast<int>(index));
    }
    families_->expandAll();
    families_->setCurrentItem(familyItem(previous >= 0 ? previous : 0));
}

void StructuredCardEditor::refreshTable()
{
    const auto familyIndex = currentFamilyIndex();
    if (familyIndex < 0) {
        table_->clear();
        table_->setRowCount(0);
        description_->setText(tr("Choose a NEC card type."));
        updateActions();
        return;
    }
    const auto& family = families()[static_cast<std::size_t>(familyIndex)];
    updating_ = true;
    const QSignalBlocker blocker(table_);
    description_->setText(family.description + tr(" Double-click a field to edit its mapped source card."));
    auto fieldCount = family.fields.size();
    for (const auto& card : document_.cards()) {
        if (belongsTo(card, family))
            fieldCount = std::max(fieldCount, static_cast<qsizetype>(card.fields.size()));
    }
    auto displayedFields = family.fields;
    for (auto& field : displayedFields)
        field.replace(QStringLiteral("(deck length)"), QStringLiteral("(%1)").arg(deckUnitLabel_));
    auto headers = QStringList{tr("Line"), tr("Card")};
    headers.append(displayedFields);
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
            auto* item = new QTableWidgetItem(value);
            if (!value.isEmpty() && !nec::necCardFieldIsNumeric(
                    card.sourceText, static_cast<std::size_t>(column))) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                const auto parameterLine = parameterControlledFields_.find(card.lineNumber);
                const auto parameterField = parameterLine == parameterControlledFields_.end()
                    ? nullptr : [&parameterLine, column] {
                        const auto found = parameterLine->second.find(
                            static_cast<std::size_t>(column));
                        return found == parameterLine->second.end() ? nullptr : &found->second;
                    }();
                if (parameterField != nullptr) {
                    styleParameterControlledField(item, table_,
                        QString::fromStdString(*parameterField));
                } else {
                    item->setToolTip(tr(
                        "This value is controlled by an expression. Edit the raw NEC source."));
                }
            }
            table_->setItem(row, column+2, item);
        }
        static_cast<void>(validateRow(row));
    }
    table_->setColumnHidden(0, false);
    updating_ = false;
    updateActions();
}

void StructuredCardEditor::updateActions()
{
    const auto familyIndex = currentFamilyIndex();
    auto addEnabled = familyIndex >= 0;
    QString disabledReason;
    if (familyIndex >= 0) {
        const auto& family = families()[static_cast<std::size_t>(familyIndex)];
        if (family.mnemonic.isEmpty()) {
            addButton_->setText(tr("Add Card in Raw Source"));
            addEnabled = false;
            disabledReason = tr("Add this card in Raw Source; existing cards can be edited here by fixed field.");
            addButton_->setEnabled(addEnabled);
            addButton_->setToolTip(disabledReason);
            deleteButton_->setEnabled(table_->currentRow() >= 0 && !table_->selectedItems().empty());
            return;
        }
        addButton_->setText(tr("Add %1 Card").arg(family.mnemonic));
        const auto alreadyExists = std::ranges::any_of(document_.cards(), [&family](const auto& card) {
            return QString::fromStdString(card.mnemonic) == family.mnemonic;
        });
        if (family.singleton && alreadyExists) {
            addEnabled = false;
            disabledReason = tr("This model already has a %1 card. Select it in the table to edit it.")
                .arg(family.mnemonic);
        }
        if (family.mnemonic == QStringLiteral("GS") && !alreadyExists) {
            addEnabled = false;
            disabledReason = tr("Use the NEC deck geometry-units selector to add GS while preserving physical dimensions.");
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
        if (!(cell->flags() & Qt::ItemIsEditable)) continue;
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
            if (error.isEmpty()) error = semanticFieldError(mnemonic, fields, fieldIndex);
        }
        cell->setBackground(error.isEmpty() ? QBrush{} : QBrush{QColor(255, 205, 205)});
        const auto choices = choicesFor(mnemonic, fieldIndex);
        cell->setToolTip(error.isEmpty()
                ? (choices.empty() ? fieldHelp(mnemonic, fieldIndex)
                                   : choiceLabel(mnemonic, fieldIndex, fields[fieldIndex]))
                : error);
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
    auto nextGeometryTag = 1;
    for (const auto& card : document_.cards()) {
        if (card.fields.empty() || (card.kind != nec::NecCardKind::GeometryWire
                && card.mnemonic != "GA" && card.mnemonic != "GH")) continue;
        const auto tag = QString::fromStdString(card.fields.front());
        if (parsesAs<int>(tag)) nextGeometryTag = std::max(nextGeometryTag, tag.toInt()+1);
    }
    if (family.mnemonic == QStringLiteral("GA"))
        return QStringLiteral("GA %1 21 1 0 180 0.001").arg(nextGeometryTag);
    if (family.mnemonic == QStringLiteral("GH"))
        return QStringLiteral("GH %1 40 0.05 0.5 0.1 0.1 0.1 0.1 0.001").arg(nextGeometryTag);
    if (family.mnemonic == QStringLiteral("SP")) return QStringLiteral("SP 0 0 0 0 0 90 0 1");
    if (family.mnemonic == QStringLiteral("SM")) return QStringLiteral("SM 4 4 0 0 0 1 0 0");
    if (family.mnemonic == QStringLiteral("SC")) return QStringLiteral("SC 0 0 1 1 0");
    if (family.mnemonic == QStringLiteral("GS")) return QStringLiteral("GS 0 0 0.3048");
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
    if (family.mnemonic == QStringLiteral("Z0")) return QStringLiteral("Z0 50");
    return {};
}

auto StructuredCardEditor::currentFamilyIndex() const -> int
{
    const auto* item = families_->currentItem();
    return item == nullptr || !item->data(0, FamilyIndexRole).isValid()
        ? -1 : item->data(0, FamilyIndexRole).toInt();
}

auto StructuredCardEditor::familyItem(int familyIndex) const -> QTreeWidgetItem*
{
    QTreeWidgetItemIterator item(families_);
    while (*item != nullptr) {
        if ((*item)->data(0, FamilyIndexRole).isValid()
            && (*item)->data(0, FamilyIndexRole).toInt() == familyIndex) return *item;
        ++item;
    }
    return nullptr;
}

}
