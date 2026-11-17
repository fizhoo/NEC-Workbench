#include "ui/geometry/GeometryPlanes.h"

#include "ui/geometry/GeometryView.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
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

GeometryPlanes::GeometryPlanes(QWidget* parent) : QSplitter(Qt::Horizontal, parent)
{
    setObjectName(QStringLiteral("geometryPlanes"));
    side_ = new QSplitter(Qt::Vertical, this);
    const std::array titles{tr("XY Plane"), tr("XZ Plane"), tr("YZ Plane")};
    for (std::size_t plane = 0; plane < Planes.size(); ++plane) {
        auto* pane = new QWidget(plane == 0 ? static_cast<QWidget*>(this) : side_);
        pane->setObjectName(QStringLiteral("geometryPane"));
        auto* layout = new QVBoxLayout(pane);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* header = new QWidget(pane);
        header->setObjectName(QStringLiteral("geometryPaneHeader"));
        header->setToolTip(tr("Double-click, or right-click → Make Main View, to show this "
                              "plane in the big view"));
        header->installEventFilter(this);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(7, 3, 5, 3);
        auto* label = new QLabel(titles[plane], header);
        auto font = label->font();
        font.setBold(true);
        label->setFont(font);
        auto* view = new GeometryView(Planes[plane], pane);
        auto* fitButton = new QToolButton(header);
        fitButton->setText(tr("Fit"));
        fitButton->setAutoRaise(true);
        connect(fitButton, &QToolButton::clicked, view, &GeometryView::fitToView);
        headerLayout->addWidget(label);
        headerLayout->addStretch();
        headerLayout->addWidget(fitButton);
        layout->addWidget(header);
        layout->addWidget(view, 1);
        views_[plane] = view;
        panes_[plane] = pane;
        headers_[plane] = header;
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
    if (plane == previous) return;
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
}

auto GeometryPlanes::eventFilter(QObject* watched, QEvent* event) -> bool
{
    const auto header = std::ranges::find(headers_, watched);
    if (header == headers_.end()) return QSplitter::eventFilter(watched, event);
    const auto plane = Planes[static_cast<std::size_t>(header - headers_.begin())];
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
