#pragma once

#include <QString>

class QTableWidgetItem;
class QWidget;

namespace necwb::ui {

void styleParameterControlledField(QTableWidgetItem* item,
    QWidget* widget, const QString& expression);

}
