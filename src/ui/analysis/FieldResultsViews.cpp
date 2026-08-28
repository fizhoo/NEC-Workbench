#include "ui/analysis/FieldResultsViews.h"

#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <ranges>
#include <vector>

namespace necwb::ui {
namespace {

auto frequencies(const auto& values) -> std::vector<double>
{
    std::vector<double> result;
    for (const auto& value : values) {
        if (std::ranges::find(result, value.frequencyMHz) == result.end()) {
            result.push_back(value.frequencyMHz);
        }
    }
    std::ranges::sort(result);
    return result;
}

auto selectedFrequency(QComboBox* control) -> double
{
    return control->currentData().toDouble();
}

void populateFrequencies(QComboBox* control, const std::vector<double>& values)
{
    control->clear();
    for (const auto value : values) {
        control->addItem(QStringLiteral("%1 MHz").arg(value, 0, 'g', 10), value);
    }
}

auto pointToSegmentDistance(const QPointF& point, const QPointF& start, const QPointF& end) -> double
{
    const auto delta = end - start;
    const auto lengthSquared = QPointF::dotProduct(delta, delta);
    if (lengthSquared <= 1.0e-12) return QLineF(point, start).length();
    const auto fraction = std::clamp(QPointF::dotProduct(point - start, delta) / lengthSquared, 0.0, 1.0);
    return QLineF(point, start + delta * fraction).length();
}

}

class CurrentPlotWidget final : public QWidget {
public:
    explicit CurrentPlotWidget(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(220); }
    void setSamples(std::vector<analysis::SegmentCurrentResult> samples)
    {
        samples_ = std::move(samples);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        const QRectF area(65, 25, std::max(1, width() - 85), std::max(1, height() - 65));
        if (samples_.empty()) {
            painter.drawText(area, Qt::AlignCenter, tr("No current samples"));
            return;
        }
        const auto maximum = std::ranges::max(samples_, {}, &analysis::SegmentCurrentResult::magnitude).magnitude;
        painter.setPen(palette().color(QPalette::Mid));
        for (auto tick = 0; tick <= 5; ++tick) {
            const auto y = area.bottom() - area.height() * tick / 5.0;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.drawText(QRectF(2, y - 10, 58, 20), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(maximum * tick / 5.0, 'g', 4));
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawRect(area);
        QPainterPath path;
        painter.setPen(QPen(QColor(44, 132, 218), 2));
        painter.setBrush(QColor(44, 132, 218));
        for (auto index = 0; index < static_cast<int>(samples_.size()); ++index) {
            const auto x = area.left() + (index + 0.5) * area.width() / samples_.size();
            const auto y = area.bottom() - samples_[index].magnitude / std::max(maximum, 1.0e-30) * area.height();
            index == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
            painter.drawEllipse(QPointF(x, y), 3, 3);
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(area.left(), height() - 30, area.width(), 20), Qt::AlignCenter,
            tr("Segment order"));
        painter.save(); painter.translate(15, area.center().y()); painter.rotate(-90);
        painter.drawText(QRectF(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
            tr("Current magnitude (A)")); painter.restore();
    }
private:
    std::vector<analysis::SegmentCurrentResult> samples_;
};

class RadiationPolarWidget final : public QWidget {
public:
    explicit RadiationPolarWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 360);
        setMouseTracking(true);
    }
    void setSamples(std::vector<QPointF> samples)
    {
        samples_ = std::move(samples);
        tracking_ = false;
        update();
    }
protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        cursor_ = event->position();
        tracking_ = true;
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
        const auto radius = std::max(1.0, std::min(width(), height()) / 2.0 - 55.0);
        const QPointF center(width() / 2.0, height() / 2.0 + 8.0);
        painter.setPen(palette().color(QPalette::Mid));
        for (auto ring = 1; ring <= 4; ++ring) {
            const auto ringRadius = radius * ring / 4;
            painter.drawEllipse(center, ringRadius, ringRadius);
            painter.drawText(QPointF(center.x() + 5, center.y() - ringRadius - 2),
                tr("%1 dB").arg((ring - 4) * 10));
        }
        for (auto angle = 0; angle < 360; angle += 30) {
            const auto radians = angle * std::numbers::pi / 180.0;
            painter.drawLine(center, center + QPointF(std::sin(radians), -std::cos(radians)) * radius);
            const auto labelPoint = center
                + QPointF(std::sin(radians), -std::cos(radians)) * (radius + 18.0);
            painter.drawText(QRectF(labelPoint.x() - 22, labelPoint.y() - 9, 44, 18),
                Qt::AlignCenter, tr("%1°").arg(angle));
        }
        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
        painter.drawLine(center - QPointF(0, radius), center + QPointF(0, radius));
        if (samples_.empty()) {
            painter.drawText(rect(), Qt::AlignCenter, tr("No radiation samples"));
            return;
        }
        const auto maxGain = std::ranges::max(samples_, {},
            [](const QPointF& point) { return point.y(); }).y();
        QPainterPath path;
        auto started = false;
        for (const auto& sample : samples_) {
            const auto clippedDb = std::max(sample.y(), maxGain - 40.0);
            const auto normalized = (clippedDb - maxGain + 40.0) / 40.0;
            const auto radians = sample.x() * std::numbers::pi / 180.0;
            const auto point = center + QPointF(std::sin(radians), -std::cos(radians)) * radius * normalized;
            started ? path.lineTo(point) : path.moveTo(point);
            started = true;
        }
        painter.setPen(QPen(QColor(215, 70, 65), 2));
        painter.drawPath(path);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(8, 20, tr("Angle axis: degrees · Radial axis: relative gain (dB) · Peak %1 dBi")
            .arg(maxGain, 0, 'f', 2));
        if (tracking_) {
            auto cursorAngle = std::atan2(cursor_.x() - center.x(), center.y() - cursor_.y())
                * 180.0 / std::numbers::pi;
            if (cursorAngle < 0.0) cursorAngle += 360.0;
            const auto circularDistance = [cursorAngle](const QPointF& sample) {
                const auto difference = std::abs(sample.x() - cursorAngle);
                return std::min(difference, 360.0 - difference);
            };
            const auto nearest = std::ranges::min_element(samples_, {}, circularDistance);
            const auto normalized = std::clamp((nearest->y() - maxGain + 40.0) / 40.0, 0.0, 1.0);
            const auto radians = nearest->x() * std::numbers::pi / 180.0;
            const auto marker = center
                + QPointF(std::sin(radians), -std::cos(radians)) * radius * normalized;
            painter.setPen(QPen(QColor(35, 115, 220), 1));
            painter.setBrush(QColor(35, 115, 220));
            painter.drawEllipse(marker, 4, 4);
            const QRectF readout(8, height() - 31, width() - 16, 23);
            painter.fillRect(readout, palette().brush(QPalette::AlternateBase));
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawRect(readout);
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(readout.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft,
                tr("Angle %1° · Gain %2 dBi · Relative %3 dB")
                    .arg(nearest->x(), 0, 'f', 1)
                    .arg(nearest->y(), 0, 'f', 2)
                    .arg(nearest->y() - maxGain, 0, 'f', 2));
        }
    }
private:
    std::vector<QPointF> samples_;
    QPointF cursor_;
    bool tracking_{};
};

class RadiationSurfaceWidget final : public QWidget {
public:
    explicit RadiationSurfaceWidget(QWidget* parent = nullptr) : QWidget(parent)
    { setMinimumSize(420, 320); setMouseTracking(true); }
    void setSamples(std::vector<analysis::RadiationSample> samples) { samples_ = std::move(samples); update(); }
    void setCurrents(std::vector<analysis::SegmentCurrentResult> currents) { currents_ = std::move(currents); update(); }
    void setModel(const model::AntennaModel& model) { model_ = model; update(); }
    void setLayerVisibility(bool antenna, bool currents, bool radiation)
    { showAntenna_ = antenna; showCurrents_ = currents; showRadiation_ = radiation; update(); }
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        last_ = event->position();
        dragging_ = true;
        panning_ = event->button() == Qt::MiddleButton
            || event->modifiers().testFlag(Qt::ShiftModifier);
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; panning_ = false; }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        cursor_ = event->position(); tracking_ = true;
        if (dragging_) {
            const auto delta = event->position() - last_; last_ = event->position();
            if (panning_) pan_ += delta;
            else {
                yaw_ += delta.x() * 0.01;
                pitch_ = std::clamp(pitch_ + delta.y() * 0.01, -1.4, 1.4);
            }
        }
        update();
    }
    void leaveEvent(QEvent*) override { tracking_ = false; update(); }
    void wheelEvent(QWheelEvent* event) override
    {
        zoom_ = std::clamp(zoom_ * std::pow(1.0015, event->angleDelta().y()), 0.35, 3.5);
        event->accept();
        update();
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        const auto scale = std::min(width(), height()) * 0.38 * zoom_;
        double maxGain{};
        if (showRadiation_ && !samples_.empty()) {
            maxGain = std::ranges::max(samples_, {}, &analysis::RadiationSample::totalGainDb).totalGainDb;
            drawRadiation(painter, scale, maxGain);
        }
        model::Point3D modelCenter;
        double modelExtent{};
        modelTransform(modelCenter, modelExtent);
        const auto antennaScale = showRadiation_ && !samples_.empty() ? scale * 0.24 : scale * 1.7;
        if (showAntenna_) drawAntenna(painter, antennaScale, modelCenter, modelExtent);
        if (showCurrents_ && !currents_.empty())
            drawCurrents(painter, antennaScale, modelCenter, modelExtent);
        painter.setPen(palette().color(QPalette::Text));
        auto status = tr("Drag to orbit · wheel to zoom · zoom %1×").arg(zoom_, 0, 'f', 2);
        if (showRadiation_ && !samples_.empty()) status += tr(" · radiation peak %1 dBi").arg(maxGain, 0, 'f', 2);
        painter.drawText(10, 22, status + tr(" · Shift/middle-drag to pan"));
        if ((!showRadiation_ || samples_.empty()) && (!showCurrents_ || currents_.empty()) && model_.empty())
            painter.drawText(rect(), Qt::AlignCenter, tr("No 3D result data"));
    }
private:
    [[nodiscard]] auto project(const model::Point3D& point, double scale) const -> QPointF
    {
        const auto cy = std::cos(yaw_), sy = std::sin(yaw_), cp = std::cos(pitch_), sp = std::sin(pitch_);
        const auto rx = cy * point.x - sy * point.y;
        const auto ry = sy * point.x + cy * point.y;
        const auto rz = cp * point.z - sp * ry;
        return {width() / 2.0 + pan_.x() + rx * scale,
            height() / 2.0 + pan_.y() - rz * scale};
    }
    void modelTransform(model::Point3D& center, double& extent) const
    {
        if (model_.empty()) { center = {}; extent = 1.0; return; }
        model::Point3D minimum = model_.wires().front().start, maximum = minimum;
        for (const auto& wire : model_.wires()) for (const auto& point : {wire.start, wire.end}) {
            minimum.x = std::min(minimum.x, point.x); minimum.y = std::min(minimum.y, point.y); minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x); maximum.y = std::max(maximum.y, point.y); maximum.z = std::max(maximum.z, point.z);
        }
        center = {(minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2, (minimum.z + maximum.z) / 2};
        extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 1.0e-12});
    }
    static auto normalized(const model::Point3D& point, const model::Point3D& center,
        double extent) -> model::Point3D
    { return {(point.x-center.x)/extent, (point.y-center.y)/extent, (point.z-center.z)/extent}; }
    void drawRadiation(QPainter& painter, double scale, double maxGain)
    {
        std::map<double, std::vector<std::pair<double, QPointF>>> phiPaths;
        std::map<double, std::vector<std::pair<double, QPointF>>> thetaPaths;
        for (const auto& sample : samples_) {
            const auto theta = sample.thetaDegrees * std::numbers::pi / 180.0;
            const auto phi = sample.phiDegrees * std::numbers::pi / 180.0;
            const auto radial = std::pow(10.0, (std::max(sample.totalGainDb, maxGain - 40.0) - maxGain) / 20.0);
            const model::Point3D point{radial * std::sin(theta) * std::cos(phi),
                radial * std::sin(theta) * std::sin(phi), radial * std::cos(theta)};
            const auto screen = project(point, scale);
            phiPaths[sample.phiDegrees].push_back({sample.thetaDegrees, screen});
            thetaPaths[sample.thetaDegrees].push_back({sample.phiDegrees, screen});
        }
        painter.setPen(QPen(QColor(80, 185, 105), 2));
        for (auto& [phi, points] : phiPaths) {
            Q_UNUSED(phi); std::ranges::sort(points, {}, &std::pair<double, QPointF>::first); QPainterPath path;
            for (auto index = 0; index < static_cast<int>(points.size()); ++index)
                index == 0 ? path.moveTo(points[index].second) : path.lineTo(points[index].second);
            painter.drawPath(path);
        }
        painter.setPen(QPen(QColor(55, 135, 85), 1));
        const auto closeAzimuth = phiPaths.size() >= 3
            && (phiPaths.rbegin()->first - phiPaths.begin()->first) >= 270.0;
        for (auto& [theta, points] : thetaPaths) {
            Q_UNUSED(theta); std::ranges::sort(points, {}, &std::pair<double, QPointF>::first); QPainterPath path;
            for (auto index = 0; index < static_cast<int>(points.size()); ++index)
                index == 0 ? path.moveTo(points[index].second) : path.lineTo(points[index].second);
            if (closeAzimuth && !points.empty()) path.lineTo(points.front().second);
            painter.drawPath(path);
        }
    }
    void drawAntenna(QPainter& painter, double scale, const model::Point3D& center, double extent)
    {
        if (model_.empty()) return;
        painter.setPen(QPen(QColor(245, 190, 45), 3));
        for (const auto& wire : model_.wires())
            painter.drawLine(project(normalized(wire.start, center, extent), scale),
                project(normalized(wire.end, center, extent), scale));
    }
    void drawCurrents(QPainter& painter, double scale, const model::Point3D& center, double extent)
    {
        const auto maximum = std::ranges::max(currents_, {}, &analysis::SegmentCurrentResult::magnitude).magnitude;
        const analysis::SegmentCurrentResult* hovered{};
        QPointF hoveredStart;
        QPointF hoveredEnd;
        auto hoverDistance = 9.0;
        for (const auto& current : currents_) {
            const auto* wire = model_.wireByTag(current.wireTag);
            if (wire == nullptr || wire->segments <= 0 || current.segment < 1 || current.segment > wire->segments) continue;
            const auto startFraction = static_cast<double>(current.segment - 1) / wire->segments;
            const auto endFraction = static_cast<double>(current.segment) / wire->segments;
            const auto interpolate = [wire](double fraction) {
                return model::Point3D{wire->start.x + (wire->end.x-wire->start.x)*fraction,
                    wire->start.y + (wire->end.y-wire->start.y)*fraction,
                    wire->start.z + (wire->end.z-wire->start.z)*fraction};
            };
            const auto start = project(normalized(interpolate(startFraction), center, extent), scale);
            const auto end = project(normalized(interpolate(endFraction), center, extent), scale);
            const auto ratio = current.magnitude / std::max(maximum, 1.0e-30);
            painter.setPen(QPen(QColor::fromHsvF((1.0-ratio)*0.67, 0.9, 0.95), 3.0 + 4.0*ratio));
            painter.drawLine(start, end);
            const auto distance = tracking_ ? pointToSegmentDistance(cursor_, start, end) : 10.0;
            if (distance < hoverDistance) {
                hoverDistance = distance; hovered = &current; hoveredStart = start; hoveredEnd = end;
            }
        }
        const QRectF legend(10, 35, 150, 12);
        for (auto offset = 0; offset < static_cast<int>(legend.width()); ++offset) {
            const auto ratio = offset / legend.width();
            painter.setPen(QColor::fromHsvF((1.0-ratio)*0.67, 0.9, 0.95));
            painter.drawLine(QPointF(legend.left()+offset, legend.top()), QPointF(legend.left()+offset, legend.bottom()));
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QPointF(legend.left(), legend.bottom()+15), tr("0 A"));
        painter.drawText(QRectF(legend.right()-70, legend.bottom()+2, 70, 20), Qt::AlignRight,
            tr("%1 A").arg(maximum, 0, 'g', 4));
        if (hovered != nullptr) {
            painter.setPen(QPen(Qt::white, 2)); painter.drawLine(hoveredStart, hoveredEnd);
            const QRectF readout(8, height()-31, width()-16, 23);
            painter.fillRect(readout, palette().brush(QPalette::AlternateBase));
            painter.setPen(palette().color(QPalette::Mid)); painter.drawRect(readout);
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(readout.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft,
                tr("Wire %1 · Segment %2 · Current %3 A · Phase %4°")
                    .arg(hovered->wireTag).arg(hovered->segment)
                    .arg(hovered->magnitude, 0, 'g', 6).arg(hovered->phaseDegrees, 0, 'f', 2));
        }
    }
    model::AntennaModel model_;
    std::vector<analysis::RadiationSample> samples_;
    std::vector<analysis::SegmentCurrentResult> currents_;
    QPointF last_;
    QPointF cursor_;
    QPointF pan_;
    double yaw_{-0.7};
    double pitch_{0.45};
    double zoom_{1.0};
    bool dragging_{};
    bool panning_{};
    bool tracking_{};
    bool showAntenna_{true};
    bool showCurrents_{true};
    bool showRadiation_{true};
};

CurrentDistributionView::CurrentDistributionView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout;
    frequency_ = new QComboBox(this); form->addRow(tr("Frequency"), frequency_);
    summary_ = new QLabel(tr("Run an analysis to populate segment currents."), this);
    plot_ = new CurrentPlotWidget(this); table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels({tr("Wire"), tr("Segment"), tr("Magnitude (A)"), tr("Phase (°)"), tr("Real"), tr("Imaginary")});
    table_->horizontalHeader()->setStretchLastSection(true); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addLayout(form); layout->addWidget(summary_); layout->addWidget(plot_, 1); layout->addWidget(table_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
}
void CurrentDistributionView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{ result_ = result; populateFrequencies(frequency_, frequencies(result.currents)); summary_->setText(tr("Segment currents from %1").arg(runDirectory)); refresh(); }
void CurrentDistributionView::refresh()
{
    std::vector<analysis::SegmentCurrentResult> values;
    for (const auto& value : result_.currents) if (value.frequencyMHz == selectedFrequency(frequency_)) values.push_back(value);
    plot_->setSamples(values); table_->setRowCount(static_cast<int>(values.size()));
    for (auto row = 0; row < static_cast<int>(values.size()); ++row) {
        const QStringList cells{QString::number(values[row].wireTag), QString::number(values[row].segment), QString::number(values[row].magnitude, 'g', 8), QString::number(values[row].phaseDegrees, 'g', 8), QString::number(values[row].current.real(), 'g', 8), QString::number(values[row].current.imag(), 'g', 8)};
        for (auto column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
}

RadiationPatternView::RadiationPatternView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout;
    frequency_ = new QComboBox(this); phi_ = new QComboBox(this); cutLabel_ = new QLabel(tr("Phi plane"), this);
    form->addRow(tr("Frequency"), frequency_); form->addRow(cutLabel_, phi_);
    auto* navigation = new QHBoxLayout;
    auto* previous = new QPushButton(tr("◀ Previous Angle"), this);
    orientationButton_ = new QPushButton(tr("Vertical Cut"), this);
    auto* next = new QPushButton(tr("Next Angle ▶"), this);
    navigation->addWidget(previous); navigation->addWidget(orientationButton_); navigation->addWidget(next); navigation->addStretch();
    summary_ = new QLabel(tr("Run an RP analysis to populate radiation patterns."), this); summary_->setWordWrap(true); plot_ = new RadiationPolarWidget(this);
    layout->addLayout(form); layout->addLayout(navigation); layout->addWidget(summary_); layout->addWidget(plot_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refreshSelectors(); });
    connect(phi_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(previous, &QPushButton::clicked, this, [this] { stepAngle(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepAngle(1); });
    connect(orientationButton_, &QPushButton::clicked, this, [this] { toggleOrientation(); });
    auto* previousShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    auto* orientationShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    for (auto* shortcut : {previousShortcut, nextShortcut, orientationShortcut})
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousShortcut, &QShortcut::activated, this, [this] { stepAngle(-1); });
    connect(nextShortcut, &QShortcut::activated, this, [this] { stepAngle(1); });
    connect(orientationShortcut, &QShortcut::activated, this, [this] { toggleOrientation(); });
}
void RadiationPatternView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{ result_ = result; populateFrequencies(frequency_, frequencies(result.radiation)); summary_->setText(tr("Far-field gain from %1 · Left/Right changes angle · Space switches vertical/horizontal").arg(runDirectory)); refreshSelectors(); }
void RadiationPatternView::refreshSelectors()
{
    const auto previousValue = phi_->currentData().toDouble();
    phi_->clear(); std::vector<double> planes;
    for (const auto& value : result_.radiation) {
        if (value.frequencyMHz != selectedFrequency(frequency_)) continue;
        auto angle = orientation_ == CutOrientation::Vertical ? value.phiDegrees : value.thetaDegrees;
        if (orientation_ == CutOrientation::Vertical) {
            angle = std::fmod(angle + 360.0, 360.0);
            if (angle >= 180.0) continue;
        }
        if (std::ranges::find(planes, angle) == planes.end()) planes.push_back(angle);
    }
    std::ranges::sort(planes); for (auto plane : planes) phi_->addItem(QStringLiteral("%1°").arg(plane, 0, 'g', 8), plane);
    const auto previousIndex = phi_->findData(previousValue); if (previousIndex >= 0) phi_->setCurrentIndex(previousIndex); refresh();
}
void RadiationPatternView::refresh()
{
    std::vector<QPointF> values; const auto angle = phi_->currentData().toDouble();
    for (const auto& value : result_.radiation) {
        if (value.frequencyMHz != selectedFrequency(frequency_)) continue;
        if (orientation_ == CutOrientation::Horizontal && value.thetaDegrees == angle)
            values.emplace_back(value.phiDegrees, value.totalGainDb);
        if (orientation_ == CutOrientation::Vertical) {
            const auto normalizedPhi = std::fmod(value.phiDegrees + 360.0, 360.0);
            if (std::abs(normalizedPhi - angle) < 1.0e-9)
                values.emplace_back(value.thetaDegrees, value.totalGainDb);
            const auto opposite = std::fmod(angle + 180.0, 360.0);
            if (std::abs(normalizedPhi - opposite) < 1.0e-9)
                values.emplace_back(360.0 - value.thetaDegrees, value.totalGainDb);
        }
    }
    std::ranges::sort(values, {}, [](const QPointF& point) { return point.x(); });
    plot_->setSamples(std::move(values));
}
void RadiationPatternView::toggleOrientation()
{
    orientation_ = orientation_ == CutOrientation::Vertical ? CutOrientation::Horizontal : CutOrientation::Vertical;
    orientationButton_->setText(orientation_ == CutOrientation::Vertical ? tr("Vertical Cut") : tr("Horizontal Cut"));
    cutLabel_->setText(orientation_ == CutOrientation::Vertical ? tr("Phi plane") : tr("Theta angle"));
    refreshSelectors();
}
void RadiationPatternView::stepAngle(int offset)
{
    if (phi_->count() == 0) return;
    phi_->setCurrentIndex((phi_->currentIndex() + offset + phi_->count()) % phi_->count());
}

Radiation3DView::Radiation3DView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout; frequency_ = new QComboBox(this); form->addRow(tr("Frequency"), frequency_);
    auto* layers = new QHBoxLayout;
    antennaControl_ = new QCheckBox(tr("Antenna"), this); antennaControl_->setChecked(true);
    currentControl_ = new QCheckBox(tr("Current Overlay"), this); currentControl_->setChecked(true);
    radiationControl_ = new QCheckBox(tr("Radiation Surface"), this); radiationControl_->setChecked(true);
    layers->addWidget(antennaControl_); layers->addWidget(currentControl_); layers->addWidget(radiationControl_); layers->addStretch();
    summary_ = new QLabel(tr("Run a multi-phi RP analysis to populate the 3D pattern."), this); surface_ = new RadiationSurfaceWidget(this);
    layout->addLayout(form); layout->addLayout(layers); layout->addWidget(summary_); layout->addWidget(surface_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    const auto updateLayers = [this] {
        surface_->setLayerVisibility(antennaControl_->isChecked(), currentControl_->isChecked(), radiationControl_->isChecked());
    };
    connect(antennaControl_, &QCheckBox::toggled, this, updateLayers);
    connect(currentControl_, &QCheckBox::toggled, this, updateLayers);
    connect(radiationControl_, &QCheckBox::toggled, this, updateLayers);
}
void Radiation3DView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{
    result_ = result;
    auto availableFrequencies = frequencies(result.radiation);
    for (const auto value : frequencies(result.currents))
        if (std::ranges::find(availableFrequencies, value) == availableFrequencies.end()) availableFrequencies.push_back(value);
    std::ranges::sort(availableFrequencies); populateFrequencies(frequency_, availableFrequencies);
    const auto samples = result.radiation.size();
    std::vector<double> planes;
    for (const auto& sample : result.radiation) if (std::ranges::find(planes, sample.phiDegrees) == planes.end()) planes.push_back(sample.phiDegrees);
    auto summary = tr("%1 current segment(s) · %2 far-field sample(s) across %3 phi plane(s) · %4")
        .arg(result.currents.size()).arg(samples).arg(planes.size()).arg(runDirectory);
    if (samples > 0 && planes.size() < 3)
        summary += tr(" · Partial radiation coverage; request Full 3D Pattern for a complete surface.");
    summary_->setText(summary);
    radiationControl_->setToolTip(planes.size() < 3
        ? tr("This run has partial radiation coverage. Use Analyze Requests → Full 3D Pattern for a complete surface.") : QString{});
    refresh();
}
void Radiation3DView::setModel(const model::AntennaModel& model)
{ model_ = model; surface_->setModel(model_); }
void Radiation3DView::refresh()
{
    std::vector<analysis::RadiationSample> radiation;
    std::vector<analysis::SegmentCurrentResult> currents;
    for (const auto& value : result_.radiation) if (value.frequencyMHz == selectedFrequency(frequency_)) radiation.push_back(value);
    for (const auto& value : result_.currents) if (value.frequencyMHz == selectedFrequency(frequency_)) currents.push_back(value);
    surface_->setSamples(std::move(radiation)); surface_->setCurrents(std::move(currents));
}

}
