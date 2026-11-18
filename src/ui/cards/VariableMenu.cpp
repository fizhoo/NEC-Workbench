#include "ui/cards/VariableMenu.h"

#include <QMenu>

#include <array>

namespace necwb::ui {

auto execVariableMenu(QWidget* parent, QPoint globalPosition, bool usesVariable) -> VariableChoice
{
    QMenu menu(parent);
    auto* variable = menu.addAction(usesVariable
        ? QObject::tr("Change Variable…") : QObject::tr("Make Variable…"));
    auto* detach = usesVariable
        ? menu.addAction(QObject::tr("Replace With Current Numeric Value")) : nullptr;
    const auto* selected = menu.exec(globalPosition);
    if (selected == nullptr) return VariableChoice::None;
    return selected == variable ? VariableChoice::Make
        : selected == detach ? VariableChoice::Detach : VariableChoice::None;
}

auto wireFieldName(std::size_t field) -> QString
{
    static const std::array names{QObject::tr("Tag"), QObject::tr("Segments"),
        QObject::tr("X1"), QObject::tr("Y1"), QObject::tr("Z1"), QObject::tr("X2"),
        QObject::tr("Y2"), QObject::tr("Z2"), QObject::tr("Radius")};
    return field < names.size() ? names[field] : QString{};
}

}
