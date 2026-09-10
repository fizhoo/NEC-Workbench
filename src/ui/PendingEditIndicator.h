#pragma once

#include <QPushButton>
#include <QStyle>

namespace necwb::ui {

inline void setPendingEditIndicator(QPushButton* button, bool pending)
{
    if (button == nullptr || button->property("pendingChanges").toBool() == pending) return;
    button->setProperty("pendingChanges", pending);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

}
