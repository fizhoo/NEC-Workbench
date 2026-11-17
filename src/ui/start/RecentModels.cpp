#include "ui/start/RecentModels.h"

#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

#include <iterator>

namespace necwb::ui::recentModels {
namespace {

const auto ListKey = QStringLiteral("files/recentModels");
const auto OpenedKey = QStringLiteral("files/recentOpened");
const auto ReopenKey = QStringLiteral("files/reopenLastModel");
const auto PendingKey = QStringLiteral("files/reopening");
constexpr auto MaximumCount = 8;

auto store(const QStringList& files) -> QStringList
{
    QSettings settings;
    settings.setValue(ListKey, files);
    // Keep the dates of listed models only.
    auto opened = settings.value(OpenedKey).toMap();
    for (auto entry = opened.begin(); entry != opened.end();)
        entry = files.contains(entry.key()) ? std::next(entry) : opened.erase(entry);
    settings.setValue(OpenedKey, opened);
    return files;
}

}

auto list() -> QStringList
{
    QStringList result;
    for (const auto& path : QSettings{}.value(ListKey).toStringList()) {
        const auto absolutePath = QFileInfo(path).absoluteFilePath();
        if (QFileInfo::exists(absolutePath) && !result.contains(absolutePath))
            result.append(absolutePath);
        if (result.size() == MaximumCount) break;
    }
    return result;
}

auto remember(const QString& path) -> QStringList
{
    if (path.isEmpty()) return list();
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    auto files = list();
    files.removeAll(absolutePath);
    files.prepend(absolutePath);
    while (files.size() > MaximumCount) files.removeLast();
    QSettings settings;
    auto opened = settings.value(OpenedKey).toMap();
    opened.insert(absolutePath, QDateTime::currentDateTime());
    settings.setValue(OpenedKey, opened);
    return store(files);
}

auto forget(const QString& path) -> QStringList
{
    auto files = list();
    files.removeAll(QFileInfo(path).absoluteFilePath());
    return store(files);
}

void clear()
{
    QSettings settings;
    settings.remove(ListKey);
    settings.remove(OpenedKey);
}

auto openedAt(const QString& path) -> QDateTime
{
    return QSettings{}.value(OpenedKey).toMap().value(path).toDateTime();
}

auto last() -> QString
{
    const auto stored = QSettings{}.value(ListKey).toStringList();
    return stored.isEmpty() ? QString{} : QFileInfo(stored.front()).absoluteFilePath();
}

auto reopenAtStartup() -> bool
{
    return QSettings{}.value(ReopenKey, true).toBool();
}

void setReopenAtStartup(bool reopen)
{
    QSettings{}.setValue(ReopenKey, reopen);
}

auto pendingReopen() -> QString
{
    return QSettings{}.value(PendingKey).toString();
}

void setPendingReopen(const QString& path)
{
    QSettings settings;
    if (path.isEmpty()) settings.remove(PendingKey);
    else settings.setValue(PendingKey, path);
    // Written straight away, so a crash while opening still leaves it.
    settings.sync();
}

}
