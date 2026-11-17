#include "ui/geometry/GeometryPreview.h"

#include "nec/NecModelConverter.h"
#include "nec/NecParser.h"
#include "nec/NecSymbolResolver.h"

#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace necwb::ui {

GeometryPreview::GeometryPreview(QWidget* host, std::function<std::string_view()> source,
    std::function<void(const model::AntennaModel&)> show)
    : QObject(host), host_(host), source_(std::move(source)), show_(std::move(show))
{
    frame_ = new QTimer(this);
    frame_->setSingleShot(true);
    frame_->setTimerType(Qt::PreciseTimer);
    connect(frame_, &QTimer::timeout, this, [this] { redraw(); });
}

void GeometryPreview::request(const std::string& variable, double value)
{
    pending_ = {variable, value};
    if (frame_->isActive()) return;
    // Right away after a pause; otherwise a frame after the last redraw.
    const auto* screen = host_->screen();
    const auto rate = screen != nullptr && screen->refreshRate() > 1.0 ? screen->refreshRate() : 60.0;
    const auto frameMs = static_cast<qint64>(std::lround(1000.0 / rate));
    const auto sinceLast = lastRedraw_.isValid() ? lastRedraw_.elapsed() : frameMs;
    frame_->start(static_cast<int>(std::max<qint64>(0, frameMs - sinceLast)));
}

void GeometryPreview::cancel()
{
    frame_->stop();
    pending_.reset();
}

void GeometryPreview::redraw()
{
    if (!pending_) return;
    lastRedraw_.start();
    const auto resolution = nec::NecSymbolResolver{}.resolve(source_(),
        {{pending_->first, pending_->second}});
    pending_.reset();
    // A value the formulas cannot use leaves the wires as they were.
    if (!resolution.ok()) return;
    show_(nec::NecModelConverter{}.convert(nec::NecParser{}.parse(resolution.resolvedSource)).model);
}

}
