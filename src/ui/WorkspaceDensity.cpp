#include "ui/WorkspaceDensity.h"

#include <QApplication>
#include <QDialog>
#include <QSettings>
#include <QWidget>

#include <algorithm>

namespace necwb::ui {
namespace {

const auto DensityKey = QStringLiteral("appearance/workspaceDensity");
constexpr auto FollowsProperty = "followsWorkspaceDensity";
constexpr auto FontDeltaProperty = "workspaceDensityFontDelta";

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

void applyWorkspaceDensityFont(QWidget* root, WorkspaceDensity density)
{
    const auto baseFontSize = workspaceDensityFontSize(density);
    const auto initialBaseFontSize = QApplication::font().pointSize();
    const auto updateFont = [baseFontSize, initialBaseFontSize](QWidget* widget) {
        if (!widget->property(FontDeltaProperty).isValid()) {
            const auto pointSize = widget->font().pointSize();
            if (pointSize > 0) widget->setProperty(FontDeltaProperty, pointSize - initialBaseFontSize);
        }
        auto font = widget->font();
        font.setPointSize(std::max(6, baseFontSize + widget->property(FontDeltaProperty).toInt()));
        widget->setFont(font);
    };
    updateFont(root);
    for (auto* widget : root->findChildren<QWidget*>()) updateFont(widget);
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
