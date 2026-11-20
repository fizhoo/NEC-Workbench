#include "ui/geometry/GeometryPlanes.h"

#include "ui/ColorScale.h"
#include "ui/ElidedLabel.h"
#include "ui/geometry/GeometryView.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr std::array Planes{geometry::ProjectionPlane::XY, geometry::ProjectionPlane::XZ,
    geometry::ProjectionPlane::YZ};

auto index(geometry::ProjectionPlane plane) -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::find(Planes, plane) - Planes.begin());
}

}

auto makeViewButtonRow(const QList<QAction*>& actions, QWidget* parent) -> QWidget*
{
    // A toolbar inside the title line, not a row of its own: when the view is narrow, the
    // buttons that do not fit go into its ›› menu instead of widening the view.
    auto* row = new QToolBar(parent);
    row->setObjectName(QStringLiteral("geometryViewButtons"));
    row->setToolButtonStyle(Qt::ToolButtonTextOnly);
    row->setIconSize({12, 12});
    row->setMovable(false);
    row->setFloatable(false);
    row->setStyleSheet(QStringLiteral("QToolBar { border: none; padding: 0; spacing: 0; }"));
    for (auto* action : actions) {
        if (action == nullptr) {
            row->addSeparator();
            continue;
        }
        row->addAction(action);
        if (auto* button = row->widgetForAction(action))
            button->setObjectName(action->objectName() + QStringLiteral("Button"));
    }
    return row;
}

GeometryPane::GeometryPane(const QString& title, GeometryInteractionView* view, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("geometryPane"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("geometryPaneHeader"));
    headerLayout_ = new QHBoxLayout(header_);
    headerLayout_->setContentsMargins(7, 1, 3, 1);
    title_ = new QLabel(title, header_);
    auto font = title_->font();
    font.setBold(true);
    title_->setFont(font);
    headerLayout_->addWidget(title_);
    headerLayout_->addStretch();
    headerLayout_->setSpacing(0);

    auto* infoRow = new QWidget(this);
    infoRow->setObjectName(QStringLiteral("geometryPaneInfo"));
    auto* infoLayout = new QHBoxLayout(infoRow);
    infoLayout->setContentsMargins(7, 1, 7, 2);
    infoLayout->setSpacing(10);
    info_ = new ElidedLabel(infoRow);
    info_->setObjectName(QStringLiteral("geometryInfoLine"));
    info_->setForegroundRole(QPalette::PlaceholderText);
    scale_ = new ColorScaleBar(infoRow);
    scale_->hide();
    infoLayout->addWidget(info_, 1);
    infoLayout->addWidget(scale_);

    layout->addWidget(header_);
    layout->addWidget(view, 1);
    layout->addWidget(infoRow);
    const auto refresh = [this, view] {
        info_->setText(view->infoText());
        info_->setToolTip(view->infoText());
    };
    connect(view, &GeometryInteractionView::infoChanged, this, refresh);
    refresh();
}

void GeometryPane::addHeaderTools(QWidget* tools)
{
    tools->setParent(header_);
    headerLayout_->addWidget(tools, 1);
}

void GeometryPane::setTitleVisible(bool visible)
{
    title_->setVisible(visible);
}

void GeometryPane::setCurrentScale(std::optional<double> maximum)
{
    scale_->setVisible(maximum.has_value());
    if (maximum) scale_->setRange(tr("|I|"), 0.0, *maximum, tr("A"));
}

GeometryPlanes::GeometryPlanes(QWidget* parent) : QSplitter(Qt::Horizontal, parent)
{
    setObjectName(QStringLiteral("geometryPlanes"));
    side_ = new QSplitter(Qt::Vertical, this);
    const std::array titles{tr("XY Top"), tr("XZ Front"), tr("YZ Side")};
    for (std::size_t plane = 0; plane < Planes.size(); ++plane) {
        auto* view = new GeometryView(Planes[plane]);
        auto* pane = new GeometryPane(titles[plane], view,
            plane == 0 ? static_cast<QWidget*>(this) : side_);
        pane->header()->setToolTip(tr("Double-click, or right-click → Make Main View, to show "
                                      "this plane in the big view"));
        pane->header()->installEventFilter(this);
        views_[plane] = view;
        panes_[plane] = pane;
        if (plane == 0) addWidget(pane);
        else side_->addWidget(pane);
    }
    addWidget(side_);
    setStretchFactor(0, 3);
    setStretchFactor(1, 2);
    side_->setStretchFactor(0, 1);
    side_->setStretchFactor(1, 1);
}

auto GeometryPlanes::mainPlane() const -> geometry::ProjectionPlane
{
    return Planes[static_cast<std::size_t>(
        std::ranges::find(panes_, widget(0)) - panes_.begin())];
}

void GeometryPlanes::showMainPlane(geometry::ProjectionPlane plane)
{
    const auto previous = mainPlane();
    if (plane != previous) {
        auto* incoming = panes_[index(plane)];
        auto* outgoing = panes_[index(previous)];
        const auto mainSizes = sizes();
        const auto sideSizes = side_->sizes();
        const auto slot = side_->indexOf(incoming);
        replaceWidget(0, incoming);
        side_->insertWidget(slot, outgoing);
        incoming->show();
        outgoing->show();
        setSizes(mainSizes);
        side_->setSizes(sideSizes);
        views_[index(plane)]->fitToView();
        views_[index(previous)]->fitToView();
        showMainTools();
    }
    if (mainPlaneChanged_) mainPlaneChanged_(plane);
}

void GeometryPlanes::setMainHeaderActions(const QList<QAction*>& actions)
{
    for (std::size_t plane = 0; plane < Planes.size(); ++plane) {
        tools_[plane] = makeViewButtonRow(actions, panes_[plane]);
        panes_[plane]->addHeaderTools(tools_[plane]);
    }
    showMainTools();
}

void GeometryPlanes::setCurrentScale(std::optional<double> maximum)
{
    currentScale_ = maximum;
    showMainTools();
}

void GeometryPlanes::setMainPlaneChangedCallback(
    std::function<void(geometry::ProjectionPlane)> callback)
{
    mainPlaneChanged_ = std::move(callback);
}

void GeometryPlanes::showMainTools()
{
    const auto main = index(mainPlane());
    for (std::size_t plane = 0; plane < Planes.size(); ++plane) {
        if (tools_[plane] != nullptr) {
            tools_[plane]->setVisible(plane == main);
            panes_[plane]->setTitleVisible(plane != main);
        }
        panes_[plane]->setCurrentScale(plane == main ? currentScale_ : std::nullopt);
    }
}

auto GeometryPlanes::eventFilter(QObject* watched, QEvent* event) -> bool
{
    const auto pane = std::ranges::find_if(panes_,
        [watched](const GeometryPane* candidate) { return candidate->header() == watched; });
    if (pane == panes_.end()) return QSplitter::eventFilter(watched, event);
    const auto plane = Planes[static_cast<std::size_t>(pane - panes_.begin())];
    if (event->type() == QEvent::MouseButtonDblClick) {
        showMainPlane(plane);
        return true;
    }
    if (event->type() == QEvent::ContextMenu) {
        QMenu menu(this);
        auto* makeMain = menu.addAction(tr("Make Main View"));
        makeMain->setEnabled(plane != mainPlane());
        if (menu.exec(static_cast<QContextMenuEvent*>(event)->globalPos()) == makeMain)
            showMainPlane(plane);
        return true;
    }
    return QSplitter::eventFilter(watched, event);
}

}
