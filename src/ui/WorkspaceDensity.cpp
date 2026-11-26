#include "ui/WorkspaceDensity.h"

#include <QDialog>
#include <QSettings>
#include <QWidget>

namespace necwb::ui {
namespace {

const auto DensityKey = QStringLiteral("appearance/workspaceDensity");
constexpr auto FollowsProperty = "followsWorkspaceDensity";

}

auto workspaceDensityFromId(const QString& id) -> WorkspaceDensity
{
    if (id == QStringLiteral("spacious")) return WorkspaceDensity::Spacious;
    if (id == QStringLiteral("standard")) return WorkspaceDensity::Standard;
    return WorkspaceDensity::Compact;
}

auto workspaceDensityId(WorkspaceDensity density) -> QString
{
    switch (density) {
    case WorkspaceDensity::Compact: return QStringLiteral("compact");
    case WorkspaceDensity::Standard: return QStringLiteral("standard");
    case WorkspaceDensity::Spacious: return QStringLiteral("spacious");
    }
    return QStringLiteral("compact");
}

auto workspaceDensityScale(WorkspaceDensity density) -> qreal
{
    switch (density) {
    case WorkspaceDensity::Compact: return 1.0 / 3.0;
    case WorkspaceDensity::Standard: return 0.5;
    case WorkspaceDensity::Spacious: return 5.0 / 6.0;
    }
    return 1.0 / 3.0;
}

auto workspaceDensityFontSize(WorkspaceDensity density) -> int
{
    switch (density) {
    case WorkspaceDensity::Compact: return 9;
    case WorkspaceDensity::Standard: return 10;
    case WorkspaceDensity::Spacious: return 11;
    }
    return 9;
}

auto savedWorkspaceDensity() -> WorkspaceDensity
{
    return workspaceDensityFromId(QSettings{}.value(DensityKey, QStringLiteral("compact")).toString());
}

void saveWorkspaceDensity(WorkspaceDensity density)
{
    QSettings{}.setValue(DensityKey, workspaceDensityId(density));
}

void followWorkspaceDensity(QWidget* window)
{
    window->setProperty(FollowsProperty, true);
}

auto followsWorkspaceDensity(const QWidget* window) -> bool
{
    return window != nullptr && window->isWindow()
        && (qobject_cast<const QDialog*>(window) != nullptr || window->property(FollowsProperty).toBool());
}

}
