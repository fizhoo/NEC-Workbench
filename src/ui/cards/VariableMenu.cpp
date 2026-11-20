#include "ui/cards/VariableMenu.h"

#include <QMenu>

#include <array>

namespace necwb::ui {

auto execVariableMenu(QWidget* parent, QPoint globalPosition, bool usesVariable,
    SourcePlacing placing) -> VariableChoice
{
    QMenu menu(parent);
    const auto along = placing == SourcePlacing::AlongWire;
    auto* variable = along ? nullptr : menu.addAction(usesVariable
        ? QObject::tr("Change Variable…") : QObject::tr("Make Variable…"));
    auto* detach = usesVariable && !along
        ? menu.addAction(QObject::tr("Replace With Current Numeric Value")) : nullptr;
    QAction* center{};
    QAction* alongWire{};
    QAction* exact{};
    if (placing != SourcePlacing::None) {
        if (!along) menu.addSeparator();
        center = menu.addAction(QObject::tr("Center of Wire (50%)"));
        alongWire = menu.addAction(QObject::tr("Along Wire…"));
        if (along) exact = menu.addAction(QObject::tr("Exact Segment"));
    }
    const auto* selected = menu.exec(globalPosition);
    if (selected == nullptr) return VariableChoice::None;
    if (selected == variable) return VariableChoice::Make;
    if (selected == detach) return VariableChoice::Detach;
    if (selected == center) return VariableChoice::Center;
    if (selected == alongWire) return VariableChoice::AlongWire;
    if (selected == exact) return VariableChoice::ExactSegment;
    return VariableChoice::None;
}

auto wireFieldName(std::size_t field) -> QString
{
    static const std::array names{QObject::tr("Tag"), QObject::tr("Segments"),
        QObject::tr("X1"), QObject::tr("Y1"), QObject::tr("Z1"), QObject::tr("X2"),
        QObject::tr("Y2"), QObject::tr("Z2"), QObject::tr("Radius")};
    return field < names.size() ? names[field] : QString{};
}

}
