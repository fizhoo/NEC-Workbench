#pragma once

#include <QIcon>
#include <QString>

namespace necwb::ui {

inline auto workbenchIcon(const char* name) -> QIcon
{
    return QIcon(QStringLiteral(":/icons/")
        + QString::fromLatin1(name) + QStringLiteral(".svg"));
}

}
