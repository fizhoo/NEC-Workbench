#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

namespace necwb::ui {

inline auto writeFile(const QString& path, const QByteArray& data) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(data) == data.size();
}

}
