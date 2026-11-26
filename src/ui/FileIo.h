#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

namespace necwb::ui {

// Writes the file, failing when not all of it reached the file: write() only buffers, and a
// short write (a full disk, a size limit) shows only when the buffer is flushed.
inline auto writeFile(const QString& path, const QByteArray& data) -> bool
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(data) == data.size() && file.flush();
}

// Writes all of the file or none of it: beside it, and renamed over it only once everything
// is flushed, so a failure leaves the file as it was. On failure `problem` says why, naming
// the folder when it cannot be written to.
inline auto writeWholeFile(const QString& path, const QByteArray& data, QIODevice::OpenMode mode,
    QString& problem) -> bool
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | mode) && file.write(data) == data.size() && file.flush()
        && file.commit())
        return true;
    const auto folder = QFileInfo(path).absolutePath();
    problem = !QFileInfo(folder).isWritable()
        ? QCoreApplication::translate("FileIo", "The folder %1 cannot be written to.")
              .arg(QDir::toNativeSeparators(folder))
        : file.errorString();
    file.cancelWriting();
    return false;
}

}
