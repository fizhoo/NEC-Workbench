#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// The recent-model list and the reopen-at-startup state, kept in QSettings.
namespace necwb::ui::recentModels {

// Up to eight existing models, newest first.
[[nodiscard]] auto list() -> QStringList;
// Puts path first and records when it was opened; returns the new list.
auto remember(const QString& path) -> QStringList;
// Drops path from the list; returns the new list.
auto forget(const QString& path) -> QStringList;
void clear();
// When path was last opened, or an invalid time for an entry older than the record.
[[nodiscard]] auto openedAt(const QString& path) -> QDateTime;
// The newest model in the list, even one whose file no longer exists.
[[nodiscard]] auto last() -> QString;

[[nodiscard]] auto reopenAtStartup() -> bool;
void setReopenAtStartup(bool reopen);
// The model being reopened at startup, set until it has finished opening; a path left here
// at the next launch did not.
[[nodiscard]] auto pendingReopen() -> QString;
void setPendingReopen(const QString& path);

}
