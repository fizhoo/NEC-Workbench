#include "ui/analysis/CurrentAntennaView.h"

#include "model/WireGeometry.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <map>

namespace necwb::ui {
namespace {

using DisplayMode = CurrentAntennaView::DisplayMode;

// Below this share of the largest current, phase is mostly numerical noise.
constexpr auto WeakCurrentRatio = 0.05;

auto amperes(double value) -> QString
{
    return QObject::tr("%1 A").arg(QString::number(value, 'g', 4));
}

auto planeName(geometry::ProjectionPlane plane) -> QString
{
    switch (plane) {
    case geometry::ProjectionPlane::XY: return QObject::tr("Top view (XY)");
    case geometry::ProjectionPlane::XZ: return QObject::tr("Front view (XZ)");
    case geometry::ProjectionPlane::YZ: return QObject::tr("Side view (YZ)");
    }
    return {};
}

auto phaseColor(double phaseDegrees) -> QColor
{
    // Cyclic hue so that -180 and +180 degrees, the same phase, share a color.
    const auto hue = std::fmod(phaseDegrees + 540.0, 360.0) / 360.0;
    return QColor::fromHsvF(static_cast<float>(hue), 0.85F, 0.9F);
}

auto distanceToSegment(QPointF point, QPointF start, QPointF end) -> double
{
    const auto line = end - start;
    const auto lengthSquared = QPointF::dotProduct(line, line);
    const auto t = lengthSquared <= 0.0 ? 0.0
        : std::clamp(QPointF::dotProduct(point - start, line) / lengthSquared, 0.0, 1.0);
    const auto closest = start + t * line;
    return std::hypot(point.x() - closest.x(), point.y() - closest.y());
}

// Closest side-by-side spacing between parallel segments of different wires in this view,
// in model units, so neighbouring envelopes can be kept from overlapping (Yagi elements sit
// close together relative to their length). Infinity when no wires run side by side.
auto parallelWireSpacing(const std::vector<analysis::DisplayedSegmentCurrent>& segments,
    geometry::ProjectionPlane plane) -> double
{
    struct Projected {
        int wireTag{};
        double x{};
        double y{};
        double directionX{};
        double directionY{};
        double length{};
    };
    std::vector<Projected> projected;
    // Sampling keeps large models fast; the closest spacing is between whole wires anyway.
    const auto step = std::max<std::size_t>(1, segments.size() / 800);
    for (auto index = std::size_t{}; index < segments.size(); index += step) {
        const auto start = geometry::project(segments[index].start, plane);
        const auto end = geometry::project(segments[index].end, plane);
        const auto dx = end.horizontal - start.horizontal;
        const auto dy = end.vertical - start.vertical;
        const auto length = std::hypot(dx, dy);
        if (length <= 0.0) continue;
        projected.push_back({segments[index].wireTag, (start.horizontal + end.horizontal) / 2.0,
            (start.vertical + end.vertical) / 2.0, dx / length, dy / length, length});
    }
    auto spacing = std::numeric_limits<double>::infinity();
    for (auto first = std::size_t{}; first < projected.size(); ++first) {
        for (auto second = first + 1; second < projected.size(); ++second) {
            const auto& a = projected[first];
            const auto& b = projected[second];
            if (a.wireTag == b.wireTag
                || std::abs(a.directionX * b.directionX + a.directionY * b.directionY) < 0.95)
                continue;
            const auto dx = b.x - a.x;
            const auto dy = b.y - a.y;
            const auto across = std::abs(a.directionX * dy - a.directionY * dx);
            const auto along = std::abs(a.directionX * dx + a.directionY * dy);
            // Collinear neighbours (a dipole's two halves) and offset segments do not crowd.
            if (across <= 1.0e-9 * (a.length + b.length) || along > (a.length + b.length) / 2.0)
                continue;
            spacing = std::min(spacing, across);
        }
    }
    return spacing;
}

}

class CurrentAntennaCanvas final : public QWidget {
public:
    explicit CurrentAntennaCanvas(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("currentsAntennaCanvas"));
        setMinimumSize(320, 240);
        setMouseTracking(true);
        setToolTip(tr("Straight wires are shown with current flowing toward +X, +Y or +Z, so "
            "elements entered in opposite directions compare correctly. Curves sit on the "
            "left of that direction; hover a segment for its values. Scroll to zoom, drag to "
            "pan, and double-click to fit the antenna again."));
    }

    void setCurrents(analysis::DisplayedCurrents currents, const model::AntennaModel& model)
    {
        // Keep the zoom while stepping through frequencies of the same antenna.
        const auto sameAntenna = model.wires().size() == model_.wires().size()
            && std::ranges::equal(model.wires(), model_.wires(), [](const auto& a, const auto& b) {
                   return a.tag == b.tag && a.start == b.start && a.end == b.end;
               });
        if (!sameAntenna) resetView();
        currents_ = std::move(currents);
        model_ = model;
        // Segments grouped per wire in order, so each wire draws one continuous curve.
        std::ranges::sort(currents_.segments, {}, [](const auto& segment) {
            return std::pair{segment.wireTag, segment.segment};
        });
        setProperty("segmentCount", static_cast<int>(currents_.segments.size()));
        updateSpacing();
    }
    void setMode(DisplayMode mode) { mode_ = mode; update(); }
    void setPlane(std::optional<geometry::ProjectionPlane> plane)
    {
        plane_ = plane;
        resetView();
        updateSpacing();
    }
    void setEnvelopeScale(double scale) { envelopeScale_ = scale; update(); }
    void setCycleDegrees(double degrees) { cycleDegrees_ = degrees; update(); }
    [[nodiscard]] auto plane() const -> geometry::ProjectionPlane
    {
        return plane_.value_or(analysis::dominantProjectionPlane(model_));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        dragging_ = true;
        lastDrag_ = event->position();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) dragging_ = false;
    }
    void mouseDoubleClickEvent(QMouseEvent*) override { resetView(); }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        cursor_ = event->position();
        tracking_ = true;
        if (dragging_) {
            pan_ += cursor_ - lastDrag_;
            lastDrag_ = cursor_;
        }
        update();
    }
    void wheelEvent(QWheelEvent* event) override
    {
        // Zoom around the cursor, so the point under it stays put.
        const auto zoom = std::clamp(zoom_ * std::pow(1.0015, event->angleDelta().y()), 0.5, 50.0);
        const auto cursor = event->position();
        pan_ = cursor - viewCenter_ - (cursor - viewCenter_ - pan_) * (zoom / zoom_);
        zoom_ = zoom;
        setProperty("zoom", zoom_);
        event->accept();
        update();
    }
    void leaveEvent(QEvent*) override
    {
        tracking_ = false;
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        const auto plane = this->plane();
        const auto bounds = geometry::projectedBounds(model_, plane);
        if (currents_.segments.empty() || !bounds || currents_.maximumMagnitude <= 0.0) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(rect(), Qt::AlignCenter, model_.wires().empty()
                ? tr("Run an analysis to see currents on the antenna.")
                : tr("No segment-current data at this frequency."));
            return;
        }

        // Leave room around the antenna for the envelopes and the header and readout text.
        const auto reserve = envelopeScale_ * 0.11 * std::min(width(), height());
        const auto inner = QRectF(rect()).adjusted(16 + reserve, 26 + reserve,
            -16 - reserve, -34 - reserve);
        const auto horizontalScale = bounds->width() > 1.0e-12
            ? inner.width() / bounds->width() : std::numeric_limits<double>::infinity();
        const auto verticalScale = bounds->height() > 1.0e-12
            ? inner.height() / bounds->height() : std::numeric_limits<double>::infinity();
        auto scale = std::min(horizontalScale, verticalScale);
        if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
        scale *= zoom_;
        viewCenter_ = inner.center();
        // Keep neighbouring envelopes apart; Scale still enlarges them on request.
        maximumOffset_ = envelopeScale_ * std::max(6.0,
            std::min(0.11 * std::min(width(), height()), 0.45 * spacing_ * scale));
        setProperty("envelopeHeight", maximumOffset_);
        const auto center = bounds->center();
        const auto toScreen = [&](const model::Point3D& point) {
            const auto projected = geometry::project(point, plane);
            return QPointF(viewCenter_.x() + pan_.x() + (projected.horizontal - center.horizontal) * scale,
                viewCenter_.y() + pan_.y() - (projected.vertical - center.vertical) * scale);
        };

        auto wireColor = palette().color(QPalette::Text);
        wireColor.setAlpha(150);
        painter.setPen(QPen(wireColor, 1.5));
        for (const auto& wire : model_.wires()) {
            for (auto index = std::size_t{1}; index < model::wirePathPointCount(wire); ++index)
                painter.drawLine(toScreen(model::wirePathPoint(wire, index - 1)),
                    toScreen(model::wirePathPoint(wire, index)));
        }

        drawSegments(painter, toScreen);
        drawHeader(painter, plane);
        if (mode_ == DisplayMode::Phase) drawPhaseLegend(painter);
        drawHoverReadout(painter, toScreen);
    }

private:
    void resetView()
    {
        zoom_ = 1.0;
        pan_ = {};
        setProperty("zoom", zoom_);
        update();
    }

    void updateSpacing()
    {
        spacing_ = parallelWireSpacing(currents_.segments, plane());
        update();
    }

    struct ScreenSegment {
        const analysis::DisplayedSegmentCurrent* current{};
        QPointF start;
        QPointF end;
        QPointF normal;
        bool hasNormal{};
    };

    template <typename ToScreen>
    auto screenSegment(const analysis::DisplayedSegmentCurrent& current, ToScreen toScreen) const
        -> ScreenSegment
    {
        ScreenSegment segment;
        segment.current = &current;
        segment.start = toScreen(current.start);
        segment.end = toScreen(current.end);
        const auto direction = segment.end - segment.start;
        const auto length = std::hypot(direction.x(), direction.y());
        // A wire seen end-on has no direction in this view, so it gets no envelope.
        if (length > 0.5) {
            segment.normal = QPointF(direction.y(), -direction.x()) / length;
            segment.hasNormal = true;
        }
        return segment;
    }

    [[nodiscard]] auto offsetFor(const analysis::DisplayedSegmentCurrent& current) const -> double
    {
        const auto value = mode_ == DisplayMode::Instantaneous
            ? analysis::instantaneousCurrent(current, cycleDegrees_) : current.magnitude;
        return value / currents_.maximumMagnitude * maximumOffset_;
    }

    template <typename ToScreen>
    void drawSegments(QPainter& painter, ToScreen toScreen)
    {
        std::map<int, std::vector<ScreenSegment>> wires;
        for (const auto& current : currents_.segments)
            wires[current.wireTag].push_back(screenSegment(current, toScreen));

        if (mode_ == DisplayMode::Phase) {
            for (const auto& [tag, segments] : wires) {
                for (const auto& segment : segments) {
                    const auto weak = segment.current->magnitude
                        < WeakCurrentRatio * currents_.maximumMagnitude;
                    painter.setPen(QPen(weak ? palette().color(QPalette::Mid)
                        : phaseColor(segment.current->relativePhaseDegrees), 5.0,
                        Qt::SolidLine, Qt::FlatCap));
                    painter.drawLine(segment.start, segment.end);
                }
            }
            return;
        }

        const auto instantaneous = mode_ == DisplayMode::Instantaneous;
        const QColor positive = instantaneous ? QColor(222, 112, 38) : QColor(44, 132, 218);
        const QColor negative(44, 132, 218);
        for (const auto& [tag, segments] : wires) {
            std::vector<QPointF> base;
            std::vector<QPointF> curve;
            std::vector<QPointF> envelope;
            std::vector<double> offsets;
            for (const auto& segment : segments) {
                if (!segment.hasNormal) continue;
                const auto middle = (segment.start + segment.end) / 2.0;
                const auto offset = offsetFor(*segment.current);
                base.push_back(middle);
                curve.push_back(middle + segment.normal * offset);
                envelope.push_back(segment.normal
                    * (segment.current->magnitude / currents_.maximumMagnitude * maximumOffset_));
                offsets.push_back(offset);
            }
            if (base.empty()) continue;
            if (base.size() == 1) {
                // A single-segment wire: span the segment so its envelope is visible.
                const auto& segment = segments.front();
                const auto shift = curve.front() - base.front();
                base = {segment.start, segment.end};
                curve = {segment.start + shift, segment.end + shift};
                envelope = {envelope.front(), envelope.front()};
                offsets = {offsets.front(), offsets.front()};
            }
            painter.setPen(Qt::NoPen);
            for (auto index = std::size_t{1}; index < base.size(); ++index) {
                auto fill = (offsets[index - 1] + offsets[index]) >= 0.0 ? positive : negative;
                fill.setAlpha(70);
                painter.setBrush(fill);
                painter.drawPolygon(QPolygonF{base[index - 1], base[index], curve[index],
                    curve[index - 1]});
            }
            painter.setBrush(Qt::NoBrush);
            if (instantaneous) {
                // The magnitude envelope stays faintly visible behind the moving current.
                auto faint = palette().color(QPalette::Text);
                faint.setAlpha(90);
                painter.setPen(QPen(faint, 1.0, Qt::DashLine));
                for (const auto sign : {1.0, -1.0}) {
                    QPainterPath path;
                    for (auto index = std::size_t{}; index < base.size(); ++index) {
                        const auto point = base[index] + sign * envelope[index];
                        index == 0 ? path.moveTo(point) : path.lineTo(point);
                    }
                    painter.drawPath(path);
                }
            }
            QPainterPath path;
            for (auto index = std::size_t{}; index < curve.size(); ++index)
                index == 0 ? path.moveTo(curve[index]) : path.lineTo(curve[index]);
            painter.setPen(QPen(instantaneous ? palette().color(QPalette::Text) : positive, 1.8));
            painter.drawPath(path);
        }
    }

    void drawHeader(QPainter& painter, geometry::ProjectionPlane plane)
    {
        const auto peak = std::ranges::max_element(currents_.segments, {},
            &analysis::DisplayedSegmentCurrent::magnitude);
        auto header = tr("%1 · peak |I| %2 at wire %3, segment %4")
            .arg(planeName(plane), amperes(peak->magnitude))
            .arg(peak->wireTag).arg(peak->segment);
        if (mode_ == DisplayMode::Instantaneous)
            header += tr(" · cycle %1°").arg(qRound(cycleDegrees_));
        // A backing strip keeps the text readable when a zoomed antenna runs underneath.
        auto backing = palette().color(QPalette::Base);
        backing.setAlpha(225);
        painter.fillRect(QRectF(0, 0, width(), 24), backing);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(8, 4, width() - 16, 20), Qt::AlignLeft | Qt::AlignVCenter, header);
        if (!tracking_) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(QRectF(8, height() - 26, width() - 16, 20),
                Qt::AlignLeft | Qt::AlignVCenter,
                tr("Scroll to zoom · drag to pan · double-click to fit"));
        }
    }

    void drawPhaseLegend(QPainter& painter)
    {
        const QRectF bar(width() - 176, height() - 52, 160, 9);
        QLinearGradient gradient(bar.topLeft(), bar.topRight());
        for (auto step = 0; step <= 12; ++step)
            gradient.setColorAt(step / 12.0, phaseColor(-180.0 + step * 30.0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRect(bar);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(palette().color(QPalette::Text));
        const QRectF labels(bar.left() - 12, bar.bottom() + 1, bar.width() + 24, 14);
        painter.drawText(labels, Qt::AlignLeft | Qt::AlignTop, QStringLiteral("−180°"));
        painter.drawText(labels, Qt::AlignHCenter | Qt::AlignTop, currents_.feedReferenced
            ? tr("0° = feed") : QStringLiteral("0°"));
        painter.drawText(labels, Qt::AlignRight | Qt::AlignTop, QStringLiteral("+180°"));
    }

    template <typename ToScreen>
    void drawHoverReadout(QPainter& painter, ToScreen toScreen)
    {
        if (!tracking_) return;
        const analysis::DisplayedSegmentCurrent* hovered{};
        auto closest = 8.0;
        for (const auto& current : currents_.segments) {
            const auto distance = distanceToSegment(cursor_, toScreen(current.start),
                toScreen(current.end));
            if (distance < closest) {
                closest = distance;
                hovered = &current;
            }
        }
        setProperty("hoverReadout", QString{});
        if (hovered == nullptr) return;
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3.0));
        painter.drawLine(toScreen(hovered->start), toScreen(hovered->end));
        auto text = tr("Wire %1 · Segment %2 · |I| %3 · Phase %4° %5")
            .arg(hovered->wireTag).arg(hovered->segment).arg(amperes(hovered->magnitude))
            .arg(QString::number(hovered->relativePhaseDegrees, 'f', 1),
                currents_.feedReferenced ? tr("rel. feed") : tr("absolute"));
        if (mode_ == DisplayMode::Instantaneous)
            text += tr(" · now %1").arg(amperes(analysis::instantaneousCurrent(*hovered, cycleDegrees_)));
        setProperty("hoverReadout", text);
        const QRectF readout(8, height() - 30, width() - 16, 22);
        painter.fillRect(readout, palette().brush(QPalette::AlternateBase));
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(readout);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(readout.adjusted(7, 0, -7, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    analysis::DisplayedCurrents currents_;
    model::AntennaModel model_;
    DisplayMode mode_{DisplayMode::Magnitude};
    std::optional<geometry::ProjectionPlane> plane_;
    double envelopeScale_{1.0};
    double cycleDegrees_{};
    double maximumOffset_{};
    double spacing_{std::numeric_limits<double>::infinity()};
    QPointF cursor_;
    QPointF viewCenter_;
    QPointF pan_;
    QPointF lastDrag_;
    double zoom_{1.0};
    bool dragging_{};
    bool tracking_{};
};

CurrentAntennaView::CurrentAntennaView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("currentsAntennaView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* controls = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("currentsDisplayMode"));
    mode_->addItem(tr("Magnitude"), static_cast<int>(DisplayMode::Magnitude));
    mode_->addItem(tr("Phase"), static_cast<int>(DisplayMode::Phase));
    mode_->addItem(tr("Instantaneous"), static_cast<int>(DisplayMode::Instantaneous));
    mode_->setItemData(0, tr("Distance from the wire shows how strong the current gets."),
        Qt::ToolTipRole);
    mode_->setItemData(1, tr("Wire color shows phase relative to the feed; weak currents are grey."),
        Qt::ToolTipRole);
    mode_->setItemData(2, tr("The actual current at one moment of the RF cycle; the side of the "
        "wire shows its direction. Press Play to watch it change."), Qt::ToolTipRole);
    plane_ = new QComboBox(this);
    plane_->setObjectName(QStringLiteral("currentsViewPlane"));
    plane_->addItem(tr("Auto"));
    plane_->addItem(tr("Top (XY)"), static_cast<int>(geometry::ProjectionPlane::XY));
    plane_->addItem(tr("Front (XZ)"), static_cast<int>(geometry::ProjectionPlane::XZ));
    plane_->addItem(tr("Side (YZ)"), static_cast<int>(geometry::ProjectionPlane::YZ));
    plane_->setToolTip(tr("Auto picks the plane the antenna mostly lies in."));
    scale_ = new QSlider(Qt::Horizontal, this);
    scale_->setObjectName(QStringLiteral("currentsEnvelopeScale"));
    scale_->setRange(25, 400);
    scale_->setValue(100);
    scale_->setMaximumWidth(140);
    scaleLabel_ = new QLabel(tr("Scale"), this);
    cycle_ = new QSlider(Qt::Horizontal, this);
    cycle_->setObjectName(QStringLiteral("currentsCycle"));
    cycle_->setRange(0, 359);
    cycleLabel_ = new QLabel(this);
    cycleLabel_->setMinimumWidth(cycleLabel_->fontMetrics().horizontalAdvance(QStringLiteral("Cycle 359°")));
    play_ = new QPushButton(tr("Play"), this);
    play_->setObjectName(QStringLiteral("currentsPlay"));
    play_->setCheckable(true);
    controls->addWidget(new QLabel(tr("Show"), this));
    controls->addWidget(mode_);
    controls->addSpacing(8);
    controls->addWidget(new QLabel(tr("View"), this));
    controls->addWidget(plane_);
    controls->addSpacing(8);
    controls->addWidget(scaleLabel_);
    controls->addWidget(scale_);
    controls->addSpacing(8);
    controls->addWidget(cycleLabel_);
    controls->addWidget(cycle_, 1);
    controls->addWidget(play_);
    controls->addStretch();
    canvas_ = new CurrentAntennaCanvas(this);
    layout->addLayout(controls);
    layout->addWidget(canvas_, 1);

    animation_ = new QTimer(this);
    animation_->setInterval(40);
    connect(animation_, &QTimer::timeout, this, [this] { advanceCycle(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        canvas_->setMode(static_cast<DisplayMode>(mode_->currentData().toInt()));
        updateControls();
    });
    connect(plane_, &QComboBox::currentIndexChanged, this, [this] {
        const auto data = plane_->currentData();
        canvas_->setPlane(data.isValid()
            ? std::optional{static_cast<geometry::ProjectionPlane>(data.toInt())} : std::nullopt);
    });
    connect(scale_, &QSlider::valueChanged, this,
        [this](int percent) { canvas_->setEnvelopeScale(percent / 100.0); });
    connect(cycle_, &QSlider::valueChanged, this, [this](int degrees) {
        canvas_->setCycleDegrees(degrees);
        cycleLabel_->setText(tr("Cycle %1°").arg(degrees));
    });
    connect(play_, &QPushButton::toggled, this, [this](bool playing) {
        play_->setText(playing ? tr("Pause") : tr("Play"));
        playing ? animation_->start() : animation_->stop();
    });
    cycleLabel_->setText(tr("Cycle %1°").arg(0));
    updateControls();
}

void CurrentAntennaView::setCurrents(analysis::DisplayedCurrents currents,
    const model::AntennaModel& model)
{
    canvas_->setCurrents(std::move(currents), model);
}

void CurrentAntennaView::updateControls()
{
    const auto mode = static_cast<DisplayMode>(mode_->currentData().toInt());
    const auto instantaneous = mode == DisplayMode::Instantaneous;
    scaleLabel_->setVisible(mode != DisplayMode::Phase);
    scale_->setVisible(mode != DisplayMode::Phase);
    cycleLabel_->setVisible(instantaneous);
    cycle_->setVisible(instantaneous);
    play_->setVisible(instantaneous);
    if (!instantaneous) play_->setChecked(false);
    mode_->setToolTip(mode_->currentData(Qt::ToolTipRole).toString());
}

void CurrentAntennaView::advanceCycle()
{
    // Skip work while the window is hidden; the cycle resumes where it was.
    if (!canvas_->isVisible()) return;
    cycle_->setValue((cycle_->value() + 6) % 360);
}

}
