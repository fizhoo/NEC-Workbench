#include "ui/ParameterFieldStyle.h"

#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QTableWidgetItem>
#include <QWidget>

#include <algorithm>

namespace necwb::ui {

void styleParameterControlledField(QTableWidgetItem* item,
    QWidget* widget, const QString& expression)
{
    if (item == nullptr || widget == nullptr) return;
    auto font = item->font();
    font.setItalic(true);
    item->setFont(font);
    auto accent = widget->palette().color(QPalette::Highlight);
    accent.setAlpha(38);
    item->setBackground(accent);

    QPixmap pixmap(22, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    auto iconFont = widget->font();
    iconFont.setBold(true);
    iconFont.setPointSize(std::max(7, iconFont.pointSize()-1));
    painter.setFont(iconFont);
    painter.setPen(widget->palette().color(QPalette::Highlight));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("ƒx"));
    item->setIcon(QIcon(pixmap));
    item->setToolTip(QObject::tr(
        "ƒx Parameter-controlled field\nExpression: %1\nEdit under Model → Parameters or in raw NEC source.")
        .arg(expression));
}

}
