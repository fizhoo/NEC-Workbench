#include "ui/cards/SymbolPrompt.h"

#include "nec/NecParser.h"
#include "nec/NecSymbolEditor.h"
#include "nec/NecSymbolResolver.h"

#include <QDialog>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace necwb::ui {
namespace {

// The box under the cell: one value per new name, Create (Enter) or Esc.
auto askValues(QWidget* parent, QPoint below, const std::vector<std::string>& names,
    const std::vector<QString>& guesses) -> std::optional<std::vector<QString>>
{
    QDialog box(parent, Qt::Popup);
    box.setObjectName(QStringLiteral("symbolPrompt"));
    auto* layout = new QVBoxLayout(&box);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    std::vector<QLineEdit*> values;
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto* value = new QLineEdit(guesses[index], &box);
        value->setObjectName(QStringLiteral("symbolValue"));
        auto* validator = new QDoubleValidator(value);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        value->setValidator(validator);
        form->addRow(QObject::tr("%1 isn't defined. Value")
            .arg(QString::fromStdString(names[index])), value);
        values.push_back(value);
    }
    layout->addLayout(form);
    auto* create = new QPushButton(QObject::tr("Create"), &box);
    create->setObjectName(QStringLiteral("symbolCreate"));
    create->setDefault(true);
    layout->addWidget(create, 0, Qt::AlignRight);
    const auto update = [&values, create] {
        create->setEnabled(std::ranges::all_of(values, [](const QLineEdit* value) {
            return value->hasAcceptableInput();
        }));
    };
    for (auto* value : values) QObject::connect(value, &QLineEdit::textChanged, &box, update);
    QObject::connect(create, &QPushButton::clicked, &box, &QDialog::accept);
    update();
    box.move(below);
    values.front()->selectAll();
    values.front()->setFocus();
    if (box.exec() != QDialog::Accepted) return std::nullopt;
    std::vector<QString> result;
    for (const auto* value : values) result.push_back(value->text().trimmed());
    return result;
}

}

auto enterFieldExpression(QWidget* parent, QPoint below, std::string_view source,
    std::size_t sourceLine, std::size_t fieldIndex, const QString& expression) -> FieldExpressionEdit
{
    const auto text = expression.trimmed().toStdString();
    if (text.empty()) return {};
    const auto resolution = nec::NecSymbolResolver{}.resolve(source);
    if (!resolution.ok()) {
        return {std::nullopt, QObject::tr("Resolve the current symbol error first: %1")
            .arg(QString::fromStdString(resolution.diagnostics.front().message))};
    }
    const auto names = nec::undefinedSymbols(text, resolution.definitions);
    auto updated = nec::replaceNecCardFieldExpression(source, sourceLine, fieldIndex, text);
    if (!updated) return {};
    if (!names.empty()) {
        // One new name in plain arithmetic starts at the value that keeps the field as it is.
        std::vector<QString> guesses(names.size());
        const auto resolved = nec::NecParser{}.parse(resolution.resolvedSource);
        if (names.size() == 1 && sourceLine > 0 && sourceLine <= resolved.cards().size()
            && fieldIndex < resolved.cards()[sourceLine - 1].fields.size()) {
            bool numeric{};
            const auto current = QString::fromStdString(
                resolved.cards()[sourceLine - 1].fields[fieldIndex]).toDouble(&numeric);
            const auto guess = numeric
                ? nec::symbolValueKeeping(text, names.front(), resolution.definitions, current)
                : std::nullopt;
            if (guess) guesses.front() = QString::number(*guess, 'g', 12);
        }
        const auto values = askValues(parent, below, names, guesses);
        if (!values) return {};
        for (std::size_t index = 0; index < names.size(); ++index)
            updated = nec::insertSymbolDefinition(*updated, names[index], (*values)[index].toStdString());
    }
    const auto check = nec::NecSymbolResolver{}.resolve(*updated);
    if (!check.ok()) {
        return {std::nullopt, QObject::tr("%1 cannot be used here: %2")
            .arg(expression.trimmed(), QString::fromStdString(check.diagnostics.front().message))};
    }
    return {std::move(updated), {}};
}

}
