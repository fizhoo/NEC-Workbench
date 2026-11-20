#include "ui/cards/SymbolPrompt.h"

#include "nec/NecCardFieldEditor.h"
#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSourceEditor.h"
#include "nec/NecSymbolEditor.h"
#include "nec/NecSymbolResolver.h"
#include "nec/SourcePositions.h"

#include <QDialog>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace necwb::ui {
namespace {

// Under the cell, shifted left to stay inside the area, or at its left edge when wider; above
// the cell when there is no room below; always on the screen.
auto placeBox(QSize size, const PromptPlace& place, const QRect& screen) -> QPoint
{
    auto x = std::max(std::min(place.cell.left(), place.area.right() + 1 - size.width()), place.area.left());
    auto y = place.cell.bottom() + 1;
    if (y + size.height() > screen.bottom() + 1) y = place.cell.top() - size.height();
    x = std::clamp(x, screen.left(), std::max(screen.left(), screen.right() + 1 - size.width()));
    y = std::clamp(y, screen.top(), std::max(screen.top(), screen.bottom() + 1 - size.height()));
    return {x, y};
}

// The box under the cell: one value per new name, Create (Enter) or Esc.
auto askValues(QWidget* parent, const PromptPlace& place, const std::vector<std::string>& names,
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
    const auto* screen = QGuiApplication::screenAt(place.cell.center());
    const auto available = (screen != nullptr ? screen : box.screen())->availableGeometry();
    box.adjustSize();
    box.move(placeBox(box.size(), place, available));
    // Placed again once shown, when its frame, if the platform gives popups one, is known.
    box.show();
    box.move(placeBox(box.frameGeometry().size(), place, available));
    values.front()->selectAll();
    values.front()->setFocus();
    if (box.exec() != QDialog::Accepted) return std::nullopt;
    std::vector<QString> result;
    for (const auto* value : values) result.push_back(value->text().trimmed());
    return result;
}

// A voltage source placed `percent` along its wire, from End 1: the card's mark is set and
// its segment field written as the segment there.
auto placeAlongWire(std::string_view source, const nec::NecCard& card, const QString& typed)
    -> FieldExpressionEdit
{
    bool numeric{};
    const auto percent = typed.chopped(1).trimmed().toDouble(&numeric);
    if (!numeric || !std::isfinite(percent) || percent < 0.0 || percent > 100.0)
        return {std::nullopt, QObject::tr("Type a position from 0% to 100% along the wire.")};
    // A segment written as an expression gives way to the segment the mark gives.
    auto line = nec::withSourcePositionMark(card.sourceText, percent);
    if (!nec::necCardFieldIsNumeric(line, 2)) {
        if (auto numbered = nec::replaceNecCardFields(line, std::array{nec::NecFieldReplacement{2, "1"}}))
            line = std::move(*numbered);
    }
    const auto marked = nec::replaceSourceLine(source, card.lineNumber, line);
    if (!marked) return {};
    auto updated = nec::refreshSourcePositions(*marked);
    const auto check = nec::NecSymbolResolver{}.resolve(updated);
    if (!check.ok())
        return {std::nullopt, QString::fromStdString(check.diagnostics.front().message)};
    const auto resolved = nec::NecParser{}.parse(check.resolvedSource);
    for (const auto& placed : nec::placeMarkedSources(resolved, nec::NecModelConverter{}.convert(resolved).model))
        if (placed.lineNumber == card.lineNumber && !placed.error.empty())
            return {std::nullopt, QString::fromStdString(placed.error)};
    return {std::move(updated), {}};
}

}

auto enterFieldExpression(QWidget* parent, const PromptPlace& place, std::string_view authored,
    std::size_t sourceLine, std::size_t fieldIndex, const QString& expression) -> FieldExpressionEdit
{
    // A voltage source's segment takes a percentage along its wire; a segment number or an
    // expression puts a source placed that way back on a fixed segment.
    std::string unmarked;
    auto source = authored;
    const auto document = nec::NecParser{}.parse(authored);
    if (sourceLine > 0 && sourceLine <= document.cards().size()) {
        const auto& card = document.cards()[sourceLine - 1];
        const auto sourceSegment = card.kind == nec::NecCardKind::Excitation && fieldIndex == 2;
        if (expression.trimmed().endsWith(QLatin1Char('%'))) {
            if (!sourceSegment)
                return {std::nullopt, QObject::tr("A percentage places a voltage source along its wire, "
                    "so it goes only in an EX card's segment field.")};
            return placeAlongWire(authored, card, expression.trimmed());
        }
        if (sourceSegment && nec::sourcePositionMark(card.sourceText)) {
            if (auto replaced = nec::replaceSourceLine(authored, sourceLine,
                    nec::withSourcePositionMark(card.sourceText, std::nullopt))) {
                unmarked = std::move(*replaced);
                source = unmarked;
            }
        }
    }
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
        const auto values = askValues(parent, place, names, guesses);
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
