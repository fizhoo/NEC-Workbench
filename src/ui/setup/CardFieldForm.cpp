#include "ui/setup/CardFieldForm.h"

#include "nec/NecCardFieldEditor.h"
#include "ui/PendingEditIndicator.h"
#include "ui/cards/CardFields.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <complex>
#include <numbers>

namespace necwb::ui {
namespace {

// Fields before this index are integers on all four cards.
constexpr auto IntegerFields = 4;

auto parses(const QString& text, bool integer) -> bool
{
    const auto value = text.trimmed().toStdString();
    const auto* end = value.data() + value.size();
    if (integer) {
        int parsed{};
        const auto [stop, error] = std::from_chars(value.data(), end, parsed);
        return !value.empty() && error == std::errc{} && stop == end;
    }
    double parsed{};
    const auto [stop, error] = std::from_chars(value.data(), end, parsed);
    return !value.empty() && error == std::errc{} && stop == end;
}

}

struct CardFieldForm::Field {
    QLabel* label{};
    QLineEdit* text{};
    QComboBox* choice{};
    bool linked{};
    bool edited{};
};

CardFieldForm::CardFieldForm(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cardFieldForm"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    heading_ = new QLabel(this);
    heading_->setWordWrap(true);
    form_ = new QFormLayout;
    form_->setVerticalSpacing(3);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("cardFieldSummary"));
    summary_->setForegroundRole(QPalette::PlaceholderText);
    notes_ = new QLabel(this);
    notes_->setObjectName(QStringLiteral("cardFieldNotes"));
    notes_->setWordWrap(true);
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #b03030;"));
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    delete_ = new QPushButton(tr("Delete"), this);
    delete_->setObjectName(QStringLiteral("deleteCardButton"));
    revert_ = new QPushButton(tr("Revert"), this);
    revert_->setToolTip(tr("Restore the card as it is in the deck (Esc)"));
    apply_ = new QPushButton(tr("Apply"), this);
    apply_->setObjectName(QStringLiteral("applyCardButton"));
    apply_->setToolTip(tr("Apply the changes (Enter)"));
    buttons->addWidget(delete_);
    buttons->addStretch();
    buttons->addWidget(revert_);
    buttons->addWidget(apply_);
    layout->addWidget(heading_);
    layout->addLayout(form_);
    layout->addWidget(summary_);
    layout->addWidget(notes_);
    layout->addWidget(error_);
    layout->addLayout(buttons);
    layout->addStretch();
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(revert_, &QPushButton::clicked, this, [this] { discardPendingEdits(); });
    connect(delete_, &QPushButton::clicked, this,
        [this] { if (deleted_) deleted_(card_.lineNumber); });
}

CardFieldForm::~CardFieldForm() = default;

auto CardFieldForm::title(const std::string& mnemonic) -> QString
{
    if (mnemonic == "EX") return tr("Source");
    if (mnemonic == "LD") return tr("Load");
    if (mnemonic == "TL") return tr("Transmission line");
    return tr("Network");
}

void CardFieldForm::setCard(const nec::NecCard& card,
    const std::vector<model::SharedSegment>& shared)
{
    QStringList notes;
    for (const auto& segment : shared) {
        if (std::ranges::any_of(segment.objects,
                [&card](const auto& object) { return object.sourceLine == card.lineNumber; }))
            notes.append(QString::fromStdString(segment.description));
    }
    card_ = card;
    heading_->setText(tr("<b>%1</b> · line %2").arg(QString::fromStdString(card.mnemonic))
        .arg(card.lineNumber));
    notes_->setText(notes.join(QLatin1Char('\n')));
    notes_->setVisible(!notes.isEmpty());
    rebuild();
}

void CardFieldForm::discardPendingEdits()
{
    rebuild();
}

void CardFieldForm::setAppliedCallback(std::function<void(std::size_t, const QString&)> callback)
{
    applied_ = std::move(callback);
}

void CardFieldForm::setDeleteCallback(std::function<void(std::size_t)> callback)
{
    deleted_ = std::move(callback);
}

void CardFieldForm::rebuild()
{
    updating_ = true;
    while (form_->rowCount() > 0) form_->removeRow(0);
    fields_.clear();
    const auto mnemonic = QString::fromStdString(card_.mnemonic);
    const auto initialType = card_.fields.empty() ? 0 : QString::fromStdString(card_.fields[0]).toInt();
    // Wide enough for any type's fields, so a type change only relabels.
    auto count = static_cast<std::size_t>(cardFieldCount(mnemonic, initialType));
    for (const auto other : {1, 4}) {
        count = std::max(count, static_cast<std::size_t>(cardFieldCount(mnemonic, other)));
    }
    count = std::max(count, card_.fields.size());
    for (std::size_t index = 0; index < count; ++index) {
        Field field;
        field.label = new QLabel(this);
        const auto value = index < card_.fields.size()
            ? QString::fromStdString(card_.fields[index]) : QString{};
        field.linked = !value.isEmpty()
            && !nec::necCardFieldIsNumeric(card_.sourceText, index);
        const auto choices = cardFieldChoices(mnemonic, static_cast<int>(index));
        QWidget* editor{};
        if (!choices.empty() && !field.linked) {
            field.choice = new QComboBox(this);
            for (const auto& choice : choices) field.choice->addItem(choice.label, choice.value);
            field.choice->setCurrentIndex(std::max(0, field.choice->findData(value)));
            const auto position = fields_.size();
            connect(field.choice, &QComboBox::currentIndexChanged, this, [this, position] {
                if (updating_) return;
                fields_[position].edited = true;
                relabel();
                setPending(true);
            });
            editor = field.choice;
        } else {
            field.text = new QLineEdit(field.linked ? tr("ƒx %1").arg(value) : value, this);
            field.text->setObjectName(QStringLiteral("cardField%1").arg(index));
            field.text->setReadOnly(field.linked);
            if (field.linked) {
                auto font = field.text->font();
                font.setItalic(true);
                field.text->setFont(font);
                field.text->setToolTip(tr("ƒx Linked to %1\nChange it under Variables, or unlink "
                    "the field in the Cards table.").arg(value));
            }
            const auto position = fields_.size();
            connect(field.text, &QLineEdit::textEdited, this, [this, position] {
                fields_[position].edited = true;
                setPending(true);
            });
            connect(field.text, &QLineEdit::returnPressed, this, [this] { apply(); });
            editor = field.text;
        }
        form_->addRow(field.label, editor);
        fields_.push_back(field);
    }
    error_->hide();
    relabel();
    updating_ = false;
    setPending(false);
}

void CardFieldForm::relabel()
{
    const auto mnemonic = QString::fromStdString(card_.mnemonic);
    const auto currentType = type();
    const auto shown = cardFieldCount(mnemonic, currentType);
    for (std::size_t index = 0; index < fields_.size(); ++index) {
        const auto label = cardFieldLabel(mnemonic, static_cast<int>(index), currentType);
        fields_[index].label->setText(label.isEmpty() ? tr("F%1").arg(index + 1) : label);
        // Fields the type does not use stay hidden unless the card has a value there.
        const auto value = fieldText(index);
        const auto used = static_cast<int>(index) < shown
            || (!value.isEmpty() && value.toDouble() != 0.0);
        form_->setRowVisible(static_cast<int>(index), used);
    }
    // A voltage source reads more easily as magnitude and phase.
    const auto voltage = card_.mnemonic == "EX" && (currentType == 0 || currentType == 5)
        && fields_.size() > 5 && parses(fieldText(4), false) && parses(fieldText(5), false);
    if (voltage) {
        const std::complex<double> volts{fieldText(4).toDouble(), fieldText(5).toDouble()};
        summary_->setText(tr("= %1 V ∠ %2°").arg(std::abs(volts), 0, 'g', 6)
            .arg(std::arg(volts) * 180.0 / std::numbers::pi, 0, 'g', 6));
    }
    summary_->setVisible(voltage);
}

auto CardFieldForm::type() const -> int
{
    if (fields_.empty()) return 0;
    return fields_.front().choice != nullptr ? fields_.front().choice->currentData().toInt()
                                             : fieldText(0).toInt();
}

auto CardFieldForm::fieldText(std::size_t index) const -> QString
{
    const auto& field = fields_[index];
    if (field.choice != nullptr) return field.choice->currentData().toString();
    if (field.linked) return QString::fromStdString(card_.fields[index]);
    return field.text->text().trimmed();
}

void CardFieldForm::apply()
{
    if (!pending_) return;
    QStringList texts;
    for (std::size_t index = 0; index < fields_.size(); ++index) {
        // An unedited field keeps the card's exact text, symbol or number.
        auto text = fields_[index].edited || index >= card_.fields.size()
            ? fieldText(index) : QString::fromStdString(card_.fields[index]);
        if (fields_[index].edited && !parses(text, static_cast<int>(index) < IntegerFields)) {
            error_->setText(tr("%1: enter %2.").arg(fields_[index].label->text(),
                static_cast<int>(index) < IntegerFields ? tr("a whole number") : tr("a number")));
            error_->show();
            return;
        }
        texts.append(text);
    }
    while (!texts.isEmpty() && texts.back().isEmpty()) texts.removeLast();
    for (auto& text : texts) if (text.isEmpty()) text = QStringLiteral("0");
    error_->hide();
    setPending(false);
    if (applied_) {
        applied_(card_.lineNumber, QString::fromStdString(card_.mnemonic) + QLatin1Char(' ')
            + texts.join(QLatin1Char(' ')));
    }
}

void CardFieldForm::setPending(bool pending)
{
    pending_ = pending;
    setPendingEditIndicator(apply_, pending);
    revert_->setEnabled(pending);
}

}
