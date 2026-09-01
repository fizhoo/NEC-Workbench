#include "ui/analysis/FieldResultsViews.h"

#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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

auto sameFrequency(double first, double second) -> bool
{
    return std::abs(first - second) <= 1.0e-9 * std::max({1.0, std::abs(first), std::abs(second)});
}

void selectFrequency(QComboBox* control, double frequencyMHz)
{
    for (auto index = 0; index < control->count(); ++index) {
        if (sameFrequency(control->itemData(index).toDouble(), frequencyMHz)) {
            control->setCurrentIndex(index);
            return;
        }
    }
    control->addItem(QStringLiteral("%1 MHz").arg(formatDecimal(frequencyMHz)), frequencyMHz);
    control->setCurrentIndex(control->count() - 1);
}

void populateFrequencies(QComboBox* control, const std::vector<double>& values)
{
    control->clear();
    for (const auto value : values) {
        control->addItem(QStringLiteral("%1 MHz").arg(formatDecimal(value)), value);
    }
}

void populateRadiationControls(QComboBox* component, QComboBox* scale, QComboBox* floor)
{
    component->addItem(QObject::tr("Total gain"), static_cast<int>(analysis::RadiationComponent::Total));
    component->addItem(QObject::tr("Vertical polarization"), static_cast<int>(analysis::RadiationComponent::Vertical));
    component->addItem(QObject::tr("Horizontal polarization"), static_cast<int>(analysis::RadiationComponent::Horizontal));
    component->addItem(QObject::tr("RHCP"), static_cast<int>(analysis::RadiationComponent::RightHandCircular));
    component->addItem(QObject::tr("LHCP"), static_cast<int>(analysis::RadiationComponent::LeftHandCircular));
    scale->addItem(QObject::tr("Normalized dB"), static_cast<int>(analysis::RadiationScale::Normalized));
    scale->addItem(QObject::tr("Absolute dBi"), static_cast<int>(analysis::RadiationScale::Absolute));
    for (const auto value : {-20.0, -30.0, -40.0, -50.0, -60.0})
        floor->addItem(QObject::tr("%1 dB").arg(formatDecimal(value)), value);
    floor->setCurrentIndex(2);
}

auto displaySettings(QComboBox* frequency, QComboBox* component, QComboBox* scale,
    QComboBox* floor) -> analysis::RadiationDisplaySettings
{
    return {selectedFrequency(frequency),
        static_cast<analysis::RadiationComponent>(component->currentData().toInt()),
        static_cast<analysis::RadiationScale>(scale->currentData().toInt()),
        floor->currentData().toDouble()};
}

auto componentName(analysis::RadiationComponent component) -> QString
{
    switch (component) {
    case analysis::RadiationComponent::Total: return QObject::tr("Total");
    case analysis::RadiationComponent::Vertical: return QObject::tr("Vertical");
    case analysis::RadiationComponent::Horizontal: return QObject::tr("Horizontal");
    case analysis::RadiationComponent::RightHandCircular: return QObject::tr("RHCP");
    case analysis::RadiationComponent::LeftHandCircular: return QObject::tr("LHCP");
    }
    return {};
}

void applyDisplaySettings(const analysis::RadiationDisplaySettings& settings,
    QComboBox* frequency, QComboBox* component, QComboBox* scale, QComboBox* floor)
{
    const QSignalBlocker frequencyBlocker(frequency);
    const QSignalBlocker componentBlocker(component);
    const QSignalBlocker scaleBlocker(scale);
    const QSignalBlocker floorBlocker(floor);
    const auto frequencyIndex = frequency->findData(settings.frequencyMHz);
    if (frequencyIndex >= 0) frequency->setCurrentIndex(frequencyIndex);
    const auto componentIndex = component->findData(static_cast<int>(settings.component));
    if (componentIndex >= 0) component->setCurrentIndex(componentIndex);
    const auto scaleIndex = scale->findData(static_cast<int>(settings.scale));
    if (scaleIndex >= 0) scale->setCurrentIndex(scaleIndex);
    const auto floorIndex = floor->findData(settings.floorDb);
    if (floorIndex >= 0) floor->setCurrentIndex(floorIndex);
}

struct CutMetrics {
    bool valid{};
    double peakAngle{};
    double peakGain{};
    double beamwidth{};
    double frontToBack{};
};

auto cutMetrics(const std::vector<QPointF>& values) -> CutMetrics
{
    CutMetrics metrics;
    if (values.empty()) return metrics;
    const auto peak = std::ranges::max_element(values, {}, [](const QPointF& point) { return point.y(); });
    metrics.valid = true;
    metrics.peakAngle = peak->x();
    metrics.peakGain = peak->y();
    const auto circularDistance = [](double first, double second) {
        const auto difference = std::fmod(std::abs(first - second), 360.0);
        return std::min(difference, 360.0 - difference);
    };
    const auto backAngle = std::fmod(metrics.peakAngle + 180.0, 360.0);
    const auto back = std::ranges::min_element(values, {}, [backAngle, circularDistance](const QPointF& point) {
        return circularDistance(point.x(), backAngle);
    });
    metrics.frontToBack = metrics.peakGain - back->y();
    const auto threshold = metrics.peakGain - 3.0;
    const auto peakIndex = static_cast<std::size_t>(std::distance(values.begin(), peak));
    auto leftIndex = peakIndex;
    auto rightIndex = peakIndex;
    auto included = std::size_t{1};
    while (included < values.size()) {
        const auto candidate = (leftIndex + values.size() - 1) % values.size();
        if (values[candidate].y() < threshold) break;
        leftIndex = candidate;
        ++included;
    }
    while (included < values.size()) {
        const auto candidate = (rightIndex + 1) % values.size();
        if (values[candidate].y() < threshold) break;
        rightIndex = candidate;
        ++included;
    }
    metrics.beamwidth = included == values.size()
        ? 360.0 : circularDistance(values[leftIndex].x(), values[rightIndex].x());
    return metrics;
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

auto resultModelExtentFromOrigin(const model::AntennaModel& model) -> double
{
    auto extent = 1.0e-12;
    for (const auto& wire : model.wires()) {
        for (const auto& point : {wire.start, wire.end})
            extent = std::max({extent, std::abs(point.x), std::abs(point.y), std::abs(point.z)});
    }
    return extent;
}

auto radiationAnglesCoverCircle(const std::vector<QPointF>& samples) -> bool
{
    std::vector<double> angles;
    angles.reserve(samples.size());
    for (const auto& sample : samples) {
        auto angle = std::fmod(sample.x(), 360.0);
        if (angle < 0.0) angle += 360.0;
        if (std::ranges::none_of(angles, [angle](double existing) {
                return std::abs(existing - angle) < 1.0e-9;
            })) {
            angles.push_back(angle);
        }
    }
    if (angles.size() < 3) return false;
    std::ranges::sort(angles);
    std::vector<double> gaps;
    gaps.reserve(angles.size());
    for (auto index = std::size_t{1}; index < angles.size(); ++index)
        gaps.push_back(angles[index] - angles[index - 1]);
    gaps.push_back(360.0 - angles.back() + angles.front());
    auto sortedGaps = gaps;
    std::ranges::sort(sortedGaps);
    const auto typicalGap = sortedGaps[sortedGaps.size() / 2];
    return typicalGap > 0.0
        && std::ranges::max(gaps) <= typicalGap * 1.5 + 1.0e-9;
}

auto connectedCurrentPaths(const std::vector<analysis::SegmentCurrentResult>& samples,
    const model::AntennaModel& model) -> std::vector<CurrentPlotPath>
{
    struct WireSeries {
        CurrentPlotPath samples;
        model::Point3D start;
        model::Point3D end;
        bool connected{};
        bool used{};
    };

    std::map<int, CurrentPlotPath> samplesByTag;
    for (const auto& sample : samples) samplesByTag[sample.wireTag].push_back(sample);

    std::vector<WireSeries> series;
    series.reserve(samplesByTag.size());
    for (auto& [tag, wireSamples] : samplesByTag) {
        std::ranges::sort(wireSamples, {}, &analysis::SegmentCurrentResult::segment);
        if (const auto* wire = model.wireByTag(tag))
            series.push_back({std::move(wireSamples), wire->start, wire->end, true});
        else
            series.push_back({std::move(wireSamples), {}, {}, false});
    }

    const auto tolerance = std::max(1.0, resultModelExtentFromOrigin(model)) * 1.0e-9;
    const auto samePoint = [tolerance](const model::Point3D& first, const model::Point3D& second) {
        return std::hypot(first.x - second.x, first.y - second.y, first.z - second.z) <= tolerance;
    };
    const auto oriented = [](const CurrentPlotPath& path, bool reverse) {
        auto result = path;
        if (reverse) std::ranges::reverse(result);
        return result;
    };

    std::vector<CurrentPlotPath> paths;
    for (auto& initial : series) {
        if (initial.used) continue;
        initial.used = true;
        auto path = initial.samples;
        if (!initial.connected) {
            paths.push_back(std::move(path));
            continue;
        }
        auto pathStart = initial.start;
        auto pathEnd = initial.end;
        auto attached = true;
        while (attached) {
            attached = false;
            for (auto& candidate : series) {
                if (candidate.used || !candidate.connected) continue;
                CurrentPlotPath addition;
                if (samePoint(candidate.start, pathEnd)) {
                    addition = oriented(candidate.samples, false);
                    pathEnd = candidate.end;
                    path.insert(path.end(), addition.begin(), addition.end());
                } else if (samePoint(candidate.end, pathEnd)) {
                    addition = oriented(candidate.samples, true);
                    pathEnd = candidate.start;
                    path.insert(path.end(), addition.begin(), addition.end());
                } else if (samePoint(candidate.start, pathStart)) {
                    addition = oriented(candidate.samples, true);
                    pathStart = candidate.end;
                    path.insert(path.begin(), addition.begin(), addition.end());
                } else if (samePoint(candidate.end, pathStart)) {
                    addition = oriented(candidate.samples, false);
                    pathStart = candidate.start;
                    path.insert(path.begin(), addition.begin(), addition.end());
                } else {
                    continue;
                }
                candidate.used = true;
                attached = true;
                break;
            }
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

class CurrentPlotWidget final : public QWidget {
public:
    explicit CurrentPlotWidget(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(220); }
    void setPaths(std::vector<CurrentPlotPath> paths)
    {
        paths_ = std::move(paths);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        const QRectF area(65, 25, std::max(1, width() - 85), std::max(1, height() - 65));
        const auto sampleCount = std::accumulate(paths_.begin(), paths_.end(), std::size_t{},
            [](std::size_t count, const auto& path) { return count + path.size(); });
        if (sampleCount == 0) {
            painter.drawText(area, Qt::AlignCenter, tr("No current samples"));
            return;
        }
        auto maximum = 0.0;
        for (const auto& path : paths_)
            for (const auto& sample : path) maximum = std::max(maximum, sample.magnitude);
        painter.setPen(palette().color(QPalette::Mid));
        for (auto tick = 0; tick <= 5; ++tick) {
            const auto y = area.bottom() - area.height() * tick / 5.0;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.drawText(QRectF(2, y - 10, 58, 20), Qt::AlignRight | Qt::AlignVCenter,
                formatDecimal(maximum * tick / 5.0));
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawRect(area);
        painter.setPen(QPen(QColor(44, 132, 218), 2));
        painter.setBrush(QColor(44, 132, 218));
        auto sampleIndex = std::size_t{};
        for (const auto& samples : paths_) {
            QPainterPath path;
            for (auto pathIndex = std::size_t{}; pathIndex < samples.size(); ++pathIndex, ++sampleIndex) {
                const auto x = area.left() + (sampleIndex + 0.5) * area.width() / sampleCount;
                const auto y = area.bottom() - samples[pathIndex].magnitude
                    / std::max(maximum, 1.0e-30) * area.height();
                pathIndex == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
                painter.drawEllipse(QPointF(x, y), 3, 3);
            }
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            painter.setBrush(QColor(44, 132, 218));
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(area.left(), height() - 30, area.width(), 20), Qt::AlignCenter,
            tr("Connected wire path"));
        painter.save(); painter.translate(15, area.center().y()); painter.rotate(-90);
        painter.drawText(QRectF(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter,
            tr("Current magnitude (A)")); painter.restore();
    }
private:
    std::vector<CurrentPlotPath> paths_;
};

class RadiationPolarWidget final : public QWidget {
public:
    explicit RadiationPolarWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("radiationPolarPlot"));
        setMinimumSize(360, 360);
        setMouseTracking(true);
    }
    void setSamples(std::vector<QPointF> samples)
    {
        samples_ = std::move(samples);
        setProperty("closedPattern", radiationAnglesCoverCircle(samples_));
        tracking_ = false;
        update();
    }
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
    {
        settings_ = settings;
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
        auto maxGain = 0.0;
        if (!samples_.empty()) {
            maxGain = std::ranges::max(samples_, {},
                [](const QPointF& point) { return point.y(); }).y();
        }
        const auto outerDb = settings_.scale == analysis::RadiationScale::Normalized
            ? 0.0 : std::ceil(maxGain / 5.0) * 5.0;
        const auto innerDb = outerDb + settings_.floorDb;
        painter.setPen(palette().color(QPalette::Mid));
        for (auto ring = 1; ring <= 4; ++ring) {
            const auto ringRadius = radius * ring / 4;
            painter.drawEllipse(center, ringRadius, ringRadius);
            const auto ringDb = innerDb + (outerDb - innerDb) * ring / 4.0;
            painter.drawText(QPointF(center.x() + 5, center.y() - ringRadius - 2),
                tr("%1 dB").arg(formatDecimal(ringDb)));
        }
        for (auto angle = 0; angle < 360; angle += 30) {
            const auto radians = angle * std::numbers::pi / 180.0;
            painter.drawLine(center, center + QPointF(std::sin(radians), -std::cos(radians)) * radius);
            const auto labelPoint = center
                + QPointF(std::sin(radians), -std::cos(radians)) * (radius + 18.0);
            painter.drawText(QRectF(labelPoint.x() - 22, labelPoint.y() - 9, 44, 18),
                Qt::AlignCenter, angle == 0 ? tr("0°/360°") : tr("%1°").arg(angle));
        }
        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
        painter.drawLine(center - QPointF(0, radius), center + QPointF(0, radius));
        if (samples_.empty()) {
            painter.drawText(rect(), Qt::AlignCenter, tr("No radiation samples"));
            return;
        }
        QPainterPath path;
        auto started = false;
        for (const auto& sample : samples_) {
            const auto displayedDb = settings_.scale == analysis::RadiationScale::Normalized
                ? sample.y() - maxGain : sample.y();
            const auto normalized = std::clamp((displayedDb - innerDb) / (outerDb - innerDb), 0.0, 1.0);
            const auto radians = sample.x() * std::numbers::pi / 180.0;
            const auto point = center + QPointF(std::sin(radians), -std::cos(radians)) * radius * normalized;
            started ? path.lineTo(point) : path.moveTo(point);
            started = true;
        }
        if (radiationAnglesCoverCircle(samples_)) path.closeSubpath();
        painter.setPen(QPen(QColor(215, 70, 65), 2));
        painter.drawPath(path);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(8, 20, tr("Angle: degrees · Radial: %1 · Peak %2 dBi")
            .arg(settings_.scale == analysis::RadiationScale::Normalized
                    ? tr("relative gain (dB)") : tr("absolute gain (dBi)"))
            .arg(formatDecimal(maxGain)));
        if (tracking_) {
            auto cursorAngle = std::atan2(cursor_.x() - center.x(), center.y() - cursor_.y())
                * 180.0 / std::numbers::pi;
            if (cursorAngle < 0.0) cursorAngle += 360.0;
            const auto circularDistance = [cursorAngle](const QPointF& sample) {
                const auto difference = std::abs(sample.x() - cursorAngle);
                return std::min(difference, 360.0 - difference);
            };
            const auto nearest = std::ranges::min_element(samples_, {}, circularDistance);
            const auto displayedDb = settings_.scale == analysis::RadiationScale::Normalized
                ? nearest->y() - maxGain : nearest->y();
            const auto normalized = std::clamp((displayedDb - innerDb) / (outerDb - innerDb), 0.0, 1.0);
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
                    .arg(formatDecimal(nearest->x()), formatDecimal(nearest->y()),
                        formatDecimal(nearest->y() - maxGain)));
        }
    }
private:
    std::vector<QPointF> samples_;
    analysis::RadiationDisplaySettings settings_;
    QPointF cursor_;
    bool tracking_{};
};

class RadiationSurfaceWidget final : public QWidget {
public:
    explicit RadiationSurfaceWidget(QWidget* parent = nullptr) : QWidget(parent)
    { setMinimumSize(420, 320); setMouseTracking(true); }
    void setSamples(std::vector<analysis::RadiationSample> samples) { samples_ = std::move(samples); update(); }
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
    { settings_ = settings; update(); }
    void setCurrents(std::vector<analysis::SegmentCurrentResult> currents) { currents_ = std::move(currents); update(); }
    void setModel(const model::AntennaModel& model) { model_ = model; update(); }
    void setLayerVisibility(bool antenna, bool currents, bool radiation)
    { showAntenna_ = antenna; showCurrents_ = currents; showRadiation_ = radiation; update(); }
    void setOverlayVisible(bool visible) { showOverlay_ = visible; update(); }
    void resetView()
    {
        yaw_ = -0.7;
        pitch_ = 0.45;
        zoom_ = 1.0;
        pan_ = {};
        update();
    }
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
        auto maxGain = -std::numeric_limits<double>::infinity();
        if (showRadiation_ && !samples_.empty()) {
            for (const auto& sample : samples_) {
                const auto gain = analysis::radiationGainDb(sample, settings_.component);
                if (std::isfinite(gain) && gain > -900.0) maxGain = std::max(maxGain, gain);
            }
        }
        const auto hasRadiation = std::isfinite(maxGain);
        if (hasRadiation) drawRadiation(painter, scale, maxGain);
        const auto modelExtent = resultModelExtentFromOrigin(model_);
        const auto antennaScale = hasRadiation ? scale * 0.32 : scale * 1.7;
        drawAxes(painter, scale * 0.34);
        if (showAntenna_) drawAntenna(painter, antennaScale, modelExtent);
        if (showCurrents_ && !currents_.empty())
            drawCurrents(painter, antennaScale, modelExtent);
        painter.setPen(palette().color(QPalette::Text));
        if (showOverlay_) {
            auto status = tr("Drag to orbit · wheel to zoom · zoom %1×").arg(formatDecimal(zoom_));
            if (hasRadiation) status += tr(" · %1 peak %2 dBi")
                .arg(componentName(settings_.component), formatDecimal(maxGain));
            painter.drawText(10, 22, status + tr(" · Shift/middle-drag to pan"));
        }
        if (hasRadiation && showOverlay_) {
            const QRectF legend(10, 42, 255, 24);
            painter.fillRect(legend, palette().brush(QPalette::AlternateBase));
            painter.setPen(QPen(QColor(80, 185, 105), 3));
            painter.drawLine(QPointF(legend.left() + 8, legend.center().y()),
                QPointF(legend.left() + 30, legend.center().y()));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(legend.adjusted(38, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft,
                tr("%1 · %2 · floor %3 dB")
                    .arg(componentName(settings_.component),
                        settings_.scale == analysis::RadiationScale::Normalized
                            ? tr("normalized") : tr("absolute"))
                    .arg(formatDecimal(settings_.floorDb)));
        }
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
    static auto normalized(const model::Point3D& point, double extent) -> model::Point3D
    { return {point.x/extent, point.y/extent, point.z/extent}; }
    void drawAxes(QPainter& painter, double scale)
    {
        const auto origin = project({}, scale);
        const std::array axes{
            std::pair{model::Point3D{1.0, 0.0, 0.0}, QColor(210, 70, 65)},
            std::pair{model::Point3D{0.0, 1.0, 0.0}, QColor(65, 175, 90)},
            std::pair{model::Point3D{0.0, 0.0, 1.0}, QColor(55, 125, 220)}};
        const std::array labels{tr("X"), tr("Y"), tr("Z")};
        for (auto index = 0U; index < axes.size(); ++index) {
            const auto end = project(axes[index].first, scale);
            painter.setPen(QPen(axes[index].second, 2));
            painter.drawLine(origin, end);
            painter.drawText(end + QPointF(4.0, -4.0), labels[index]);
        }
    }
    void drawRadiation(QPainter& painter, double scale, double maxGain)
    {
        std::map<double, std::vector<std::pair<double, QPointF>>> phiPaths;
        std::map<double, std::vector<std::pair<double, QPointF>>> thetaPaths;
        for (const auto& sample : samples_) {
            const auto gain = analysis::radiationGainDb(sample, settings_.component);
            if (!std::isfinite(gain) || gain <= -900.0) continue;
            const auto theta = sample.thetaDegrees * std::numbers::pi / 180.0;
            const auto phi = sample.phiDegrees * std::numbers::pi / 180.0;
            const auto reference = settings_.scale == analysis::RadiationScale::Normalized
                ? maxGain : std::ceil(maxGain / 5.0) * 5.0;
            const auto radial = std::pow(10.0,
                (std::max(gain, reference + settings_.floorDb) - reference) / 20.0);
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
    void drawAntenna(QPainter& painter, double scale, double extent)
    {
        if (model_.empty()) return;
        painter.setPen(QPen(QColor(245, 190, 45), 3));
        for (const auto& wire : model_.wires())
            painter.drawLine(project(normalized(wire.start, extent), scale),
                project(normalized(wire.end, extent), scale));
    }
    void drawCurrents(QPainter& painter, double scale, double extent)
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
            const auto start = project(normalized(interpolate(startFraction), extent), scale);
            const auto end = project(normalized(interpolate(endFraction), extent), scale);
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
            tr("%1 A").arg(formatDecimal(maximum)));
        if (hovered != nullptr) {
            painter.setPen(QPen(Qt::white, 2)); painter.drawLine(hoveredStart, hoveredEnd);
            const QRectF readout(8, height()-31, width()-16, 23);
            painter.fillRect(readout, palette().brush(QPalette::AlternateBase));
            painter.setPen(palette().color(QPalette::Mid)); painter.drawRect(readout);
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(readout.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft,
                tr("Wire %1 · Segment %2 · Current %3 A · Phase %4°")
                    .arg(hovered->wireTag).arg(hovered->segment)
                    .arg(formatDecimal(hovered->magnitude), formatDecimal(hovered->phaseDegrees)));
        }
    }
    model::AntennaModel model_;
    std::vector<analysis::RadiationSample> samples_;
    std::vector<analysis::SegmentCurrentResult> currents_;
    analysis::RadiationDisplaySettings settings_;
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
    bool showOverlay_{true};
};

CurrentDistributionView::CurrentDistributionView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    frequency_ = new QComboBox(this); frequency_->hide();
    summary_ = new QLabel(tr("Run an analysis to populate segment currents."), this);
    plot_ = new CurrentPlotWidget(this); table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels({tr("Wire"), tr("Segment"), tr("Magnitude (A)"), tr("Phase (°)"), tr("Real"), tr("Imaginary")});
    table_->horizontalHeader()->setStretchLastSection(true); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(summary_); layout->addWidget(plot_, 1); layout->addWidget(table_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
}
void CurrentDistributionView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{ result_ = result; runContext_ = runDirectory; populateFrequencies(frequency_, frequencies(result.currents)); refresh(); }
void CurrentDistributionView::setModel(const model::AntennaModel& model)
{ model_ = model; refresh(); }
void CurrentDistributionView::setSelectedFrequency(double frequencyMHz)
{ selectFrequency(frequency_, frequencyMHz); refresh(); }
void CurrentDistributionView::refresh()
{
    std::vector<analysis::SegmentCurrentResult> values;
    for (const auto& value : result_.currents) if (value.frequencyMHz == selectedFrequency(frequency_)) values.push_back(value);
    summary_->setText(values.empty()
        ? tr("%1 MHz · No segment-current data · %2")
            .arg(formatDecimal(selectedFrequency(frequency_)), runContext_)
        : tr("%1 MHz · %2 current segment(s) · %3")
            .arg(formatDecimal(selectedFrequency(frequency_))).arg(values.size()).arg(runContext_));
    plot_->setPaths(connectedCurrentPaths(values, model_)); table_->setRowCount(static_cast<int>(values.size()));
    for (auto row = 0; row < static_cast<int>(values.size()); ++row) {
        const QStringList cells{QString::number(values[row].wireTag),
            QString::number(values[row].segment), formatDecimal(values[row].magnitude),
            formatDecimal(values[row].phaseDegrees), formatDecimal(values[row].current.real()),
            formatDecimal(values[row].current.imag())};
        for (auto column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
}

RadiationPatternView::RadiationPatternView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this); auto* form = new QFormLayout;
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation2DFrequency")); frequency_->hide();
    phi_ = new QComboBox(this); phi_->setObjectName(QStringLiteral("radiation2DCutPlane"));
    cutLabel_ = new QLabel(tr("Phi plane"), this);
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation2DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation2DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation2DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addRow(tr("Component"), component_);
    form->addRow(tr("Scale"), scale_); form->addRow(tr("Dynamic range"), floor_);
    form->addRow(cutLabel_, phi_);
    auto* navigation = new QHBoxLayout;
    auto* previous = new QPushButton(tr("◀ Previous Angle"), this);
    orientationButton_ = new QPushButton(tr("Vertical Cut"), this);
    orientationButton_->setObjectName(QStringLiteral("radiation2DOrientation"));
    auto* next = new QPushButton(tr("Next Angle ▶"), this);
    maxGainCutButton_ = new QPushButton(tr("Show Max-Gain Cut"), this);
    maxGainCutButton_->setObjectName(QStringLiteral("radiation2DMaxGainCut"));
    exportImageButton_ = new QPushButton(tr("Export Image…"), this);
    exportImageButton_->setObjectName(QStringLiteral("radiation2DExportImage"));
    exportDataButton_ = new QPushButton(tr("Export CSV…"), this);
    exportDataButton_->setObjectName(QStringLiteral("radiation2DExportData"));
    navigation->addWidget(previous); navigation->addWidget(orientationButton_); navigation->addWidget(next);
    navigation->addWidget(maxGainCutButton_);
    navigation->addStretch(); navigation->addWidget(exportImageButton_); navigation->addWidget(exportDataButton_);
    summary_ = new QLabel(tr("Run an RP analysis to populate radiation patterns."), this);
    summary_->setObjectName(QStringLiteral("radiation2DSummary"));
    summary_->setWordWrap(true); plot_ = new RadiationPolarWidget(this);
    layout->addLayout(form); layout->addLayout(navigation); layout->addWidget(summary_); layout->addWidget(plot_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refreshSelectors(); settingsChanged(); });
    connect(component_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(floor_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(phi_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(previous, &QPushButton::clicked, this, [this] { stepAngle(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepAngle(1); });
    connect(orientationButton_, &QPushButton::clicked, this, [this] { toggleOrientation(); });
    connect(maxGainCutButton_, &QPushButton::clicked, this, [this] { showMaxGainCut(); });
    connect(exportImageButton_, &QPushButton::clicked, this, [this] { exportImage(); });
    connect(exportDataButton_, &QPushButton::clicked, this, [this] { exportData(); });
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
{
    const auto previous = displaySettings(frequency_, component_, scale_, floor_);
    result_ = result;
    runContext_ = runDirectory;
    const QSignalBlocker blocker(frequency_);
    populateFrequencies(frequency_, frequencies(result.radiation));
    applyDisplaySettings(previous, frequency_, component_, scale_, floor_);
    refreshSelectors();
}
void RadiationPatternView::setSelectedFrequency(double frequencyMHz)
{
    const QSignalBlocker blocker(frequency_);
    selectFrequency(frequency_, frequencyMHz);
    refreshSelectors();
}
void RadiationPatternView::setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
{
    updatingSettings_ = true;
    applyDisplaySettings(settings, frequency_, component_, scale_, floor_);
    updatingSettings_ = false;
    refreshSelectors();
}
void RadiationPatternView::setSettingsChangedCallback(SettingsChangedCallback callback)
{ settingsChangedCallback_ = std::move(callback); }
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
    std::ranges::sort(planes); for (auto plane : planes)
        phi_->addItem(QStringLiteral("%1°").arg(formatDecimal(plane)), plane);
    const auto previousIndex = phi_->findData(previousValue); if (previousIndex >= 0) phi_->setCurrentIndex(previousIndex); refresh();
}
void RadiationPatternView::refresh()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    std::vector<QPointF> values; const auto angle = phi_->currentData().toDouble();
    for (const auto& value : result_.radiation) {
        if (value.frequencyMHz != selectedFrequency(frequency_)) continue;
        if (orientation_ == CutOrientation::Horizontal && value.thetaDegrees == angle)
            values.emplace_back(value.phiDegrees, analysis::radiationGainDb(value, settings.component));
        if (orientation_ == CutOrientation::Vertical) {
            const auto normalizedPhi = std::fmod(value.phiDegrees + 360.0, 360.0);
            if (std::abs(normalizedPhi - angle) < 1.0e-9)
                values.emplace_back(value.thetaDegrees, analysis::radiationGainDb(value, settings.component));
            const auto opposite = std::fmod(angle + 180.0, 360.0);
            if (std::abs(normalizedPhi - opposite) < 1.0e-9)
                values.emplace_back(360.0 - value.thetaDegrees, analysis::radiationGainDb(value, settings.component));
        }
    }
    std::erase_if(values, [](const QPointF& point) { return !std::isfinite(point.y()) || point.y() <= -900.0; });
    std::ranges::sort(values, {}, [](const QPointF& point) { return point.x(); });
    const auto metrics = cutMetrics(values);
    if (metrics.valid) {
        summary_->setText(tr("%1 MHz · %2 · %3 peak %4 dBi at %5° · 3 dB beamwidth %6° · F/B %7 dB · Left/Right changes angle · Space changes cut")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, componentName(settings.component))
            .arg(formatDecimal(metrics.peakGain), formatDecimal(metrics.peakAngle),
                formatDecimal(metrics.beamwidth), formatDecimal(metrics.frontToBack)));
    } else {
        summary_->setText(tr("%1 MHz · No %2 samples for this cut · %3")
            .arg(formatDecimal(settings.frequencyMHz), componentName(settings.component), runContext_));
    }
    exportImageButton_->setEnabled(metrics.valid);
    exportDataButton_->setEnabled(metrics.valid);
    maxGainCutButton_->setEnabled(std::ranges::any_of(result_.radiation, [settings](const auto& sample) {
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return sameFrequency(sample.frequencyMHz, settings.frequencyMHz)
            && std::isfinite(gain) && gain > -900.0;
    }));
    plot_->setDisplaySettings(settings);
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
void RadiationPatternView::showMaxGainCut()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    const auto peak = std::ranges::max_element(result_.radiation, {}, [settings](const auto& sample) {
        if (!sameFrequency(sample.frequencyMHz, settings.frequencyMHz))
            return -std::numeric_limits<double>::infinity();
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return std::isfinite(gain) && gain > -900.0
            ? gain : -std::numeric_limits<double>::infinity();
    });
    if (peak == result_.radiation.end()
        || !sameFrequency(peak->frequencyMHz, settings.frequencyMHz)) return;

    orientation_ = CutOrientation::Vertical;
    orientationButton_->setText(tr("Vertical Cut"));
    cutLabel_->setText(tr("Phi plane"));
    refreshSelectors();
    auto peakPlane = std::fmod(peak->phiDegrees + 360.0, 360.0);
    if (peakPlane >= 180.0) peakPlane -= 180.0;
    auto nearestIndex = -1;
    auto nearestDistance = std::numeric_limits<double>::infinity();
    for (auto index = 0; index < phi_->count(); ++index) {
        const auto distance = std::abs(phi_->itemData(index).toDouble() - peakPlane);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestIndex = index;
        }
    }
    if (nearestIndex >= 0) phi_->setCurrentIndex(nearestIndex);
}
void RadiationPatternView::settingsChanged()
{
    if (!updatingSettings_ && settingsChangedCallback_)
        settingsChangedCallback_(displaySettings(frequency_, component_, scale_, floor_));
}

void RadiationPatternView::exportImage()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Export 2D Radiation Pattern"),
        QStringLiteral("radiation-2d.png"), tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (!path.isEmpty()) plot_->grab().save(path);
}

void RadiationPatternView::exportData()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Export 2D Radiation Data"),
        QStringLiteral("radiation-2d.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty()) return;
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    const auto selectedPlane = phi_->currentData().toDouble();
    QString output = QStringLiteral("frequency_mhz,cut,plane_degrees,angle_degrees,gain_dbi\n");
    for (const auto& sample : result_.radiation) {
        if (!sameFrequency(sample.frequencyMHz, settings.frequencyMHz)) continue;
        auto included = false;
        auto angle = 0.0;
        if (orientation_ == CutOrientation::Horizontal
            && sameFrequency(sample.thetaDegrees, selectedPlane)) {
            included = true;
            angle = sample.phiDegrees;
        }
        if (orientation_ == CutOrientation::Vertical) {
            const auto phi = std::fmod(sample.phiDegrees + 360.0, 360.0);
            const auto opposite = std::fmod(selectedPlane + 180.0, 360.0);
            if (sameFrequency(phi, selectedPlane)) {
                included = true;
                angle = sample.thetaDegrees;
            } else if (sameFrequency(phi, opposite)) {
                included = true;
                angle = 360.0 - sample.thetaDegrees;
            }
        }
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        if (included && std::isfinite(gain) && gain > -900.0) {
            output += QStringLiteral("%1,%2,%3,%4,%5\n")
                .arg(settings.frequencyMHz, 0, 'g', 15)
                .arg(orientation_ == CutOrientation::Vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal"))
                .arg(selectedPlane, 0, 'g', 15).arg(angle, 0, 'g', 15).arg(gain, 0, 'g', 15);
        }
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(output.toUtf8());
}

Radiation3DView::Radiation3DView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    controls_ = new QWidget(this);
    controls_->setObjectName(QStringLiteral("radiation3DControls"));
    auto* controlsLayout = new QVBoxLayout(controls_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout;
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation3DFrequency")); frequency_->hide();
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation3DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation3DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation3DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addRow(tr("Component"), component_);
    form->addRow(tr("Scale"), scale_); form->addRow(tr("Dynamic range"), floor_);
    auto* layers = new QHBoxLayout;
    antennaControl_ = new QCheckBox(tr("Antenna"), this); antennaControl_->setChecked(true);
    currentControl_ = new QCheckBox(tr("Current Overlay"), this); currentControl_->setChecked(true);
    radiationControl_ = new QCheckBox(tr("Radiation Surface"), this); radiationControl_->setChecked(true);
    auto* resetView = new QPushButton(tr("Reset View"), this);
    exportImageButton_ = new QPushButton(tr("Export Image…"), this);
    exportImageButton_->setObjectName(QStringLiteral("radiation3DExportImage"));
    exportDataButton_ = new QPushButton(tr("Export CSV…"), this);
    exportDataButton_->setObjectName(QStringLiteral("radiation3DExportData"));
    layers->addWidget(antennaControl_); layers->addWidget(currentControl_); layers->addWidget(radiationControl_);
    layers->addStretch(); layers->addWidget(resetView); layers->addWidget(exportImageButton_); layers->addWidget(exportDataButton_);
    summary_ = new QLabel(tr("Run a multi-phi RP analysis to populate the 3D pattern."), this);
    summary_->setObjectName(QStringLiteral("radiation3DSummary")); summary_->setWordWrap(true);
    surface_ = new RadiationSurfaceWidget(this);
    controlsLayout->addLayout(form);
    controlsLayout->addLayout(layers);
    layout->addWidget(controls_);
    layout->addWidget(summary_);
    layout->addWidget(surface_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(component_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    connect(floor_, &QComboBox::currentIndexChanged, this, [this] { refresh(); settingsChanged(); });
    const auto updateLayers = [this] {
        surface_->setLayerVisibility(antennaControl_->isChecked(), currentControl_->isChecked(), radiationControl_->isChecked());
    };
    connect(antennaControl_, &QCheckBox::toggled, this, updateLayers);
    connect(currentControl_, &QCheckBox::toggled, this, updateLayers);
    connect(radiationControl_, &QCheckBox::toggled, this, updateLayers);
    connect(resetView, &QPushButton::clicked, surface_, [this] { surface_->resetView(); });
    connect(exportImageButton_, &QPushButton::clicked, this, [this] { exportImage(); });
    connect(exportDataButton_, &QPushButton::clicked, this, [this] { exportData(); });
}
void Radiation3DView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{
    const auto previous = displaySettings(frequency_, component_, scale_, floor_);
    result_ = result;
    runContext_ = runDirectory;
    auto availableFrequencies = frequencies(result.radiation);
    for (const auto value : frequencies(result.currents))
        if (std::ranges::find(availableFrequencies, value) == availableFrequencies.end()) availableFrequencies.push_back(value);
    std::ranges::sort(availableFrequencies);
    const QSignalBlocker blocker(frequency_);
    populateFrequencies(frequency_, availableFrequencies);
    applyDisplaySettings(previous, frequency_, component_, scale_, floor_);
    std::vector<double> planes;
    for (const auto& sample : result.radiation) if (std::ranges::find(planes, sample.phiDegrees) == planes.end()) planes.push_back(sample.phiDegrees);
    radiationControl_->setToolTip(planes.size() < 3
        ? tr("This run has partial radiation coverage. Use Analyze Requests → Full 3D Pattern for a complete surface.") : QString{});
    refresh();
}
void Radiation3DView::setModel(const model::AntennaModel& model)
{ model_ = model; surface_->setModel(model_); }
void Radiation3DView::setSelectedFrequency(double frequencyMHz)
{
    const QSignalBlocker blocker(frequency_);
    selectFrequency(frequency_, frequencyMHz);
    refresh();
}
void Radiation3DView::setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
{
    updatingSettings_ = true;
    applyDisplaySettings(settings, frequency_, component_, scale_, floor_);
    updatingSettings_ = false;
    refresh();
}
void Radiation3DView::setSettingsChangedCallback(SettingsChangedCallback callback)
{ settingsChangedCallback_ = std::move(callback); }
void Radiation3DView::setOverviewMode(bool enabled)
{
    controls_->setVisible(!enabled);
    summary_->setVisible(!enabled);
    surface_->setOverlayVisible(!enabled);
    layout()->setContentsMargins(enabled ? 0 : 11, enabled ? 0 : 11,
        enabled ? 0 : 11, enabled ? 0 : 11);
}
void Radiation3DView::refresh()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    std::vector<analysis::RadiationSample> radiation;
    std::vector<analysis::SegmentCurrentResult> currents;
    for (const auto& value : result_.radiation) if (value.frequencyMHz == selectedFrequency(frequency_)) radiation.push_back(value);
    for (const auto& value : result_.currents) if (value.frequencyMHz == selectedFrequency(frequency_)) currents.push_back(value);
    const auto metrics = analysis::radiationMetrics(radiation, settings.component);
    std::vector<double> planes;
    for (const auto& sample : radiation)
        if (std::ranges::find(planes, sample.phiDegrees) == planes.end()) planes.push_back(sample.phiDegrees);
    if (metrics.valid) {
        auto text = tr("%1 MHz · %2 · %3 peak %4 dBi at θ %5°, φ %6° · F/B %7 dB · %8 phi plane(s)")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, componentName(settings.component))
            .arg(formatDecimal(metrics.peakGainDb), formatDecimal(metrics.peakThetaDegrees),
                formatDecimal(metrics.peakPhiDegrees), formatDecimal(metrics.frontToBackDb))
            .arg(planes.size());
        if (planes.size() < 3)
            text += tr(" · Partial coverage; request Full 3D Pattern for a complete surface.");
        summary_->setText(text);
    } else {
        summary_->setText(tr("%1 MHz · %2 · No %3 radiation samples · %4 current segment(s)")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, componentName(settings.component))
            .arg(currents.size()));
    }
    exportImageButton_->setEnabled(!radiation.empty() || !currents.empty() || !model_.empty());
    exportDataButton_->setEnabled(!radiation.empty());
    surface_->setDisplaySettings(settings);
    surface_->setSamples(std::move(radiation)); surface_->setCurrents(std::move(currents));
}
void Radiation3DView::settingsChanged()
{
    if (!updatingSettings_ && settingsChangedCallback_)
        settingsChangedCallback_(displaySettings(frequency_, component_, scale_, floor_));
}

void Radiation3DView::exportImage()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Export 3D Results"),
        QStringLiteral("radiation-3d.png"), tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (!path.isEmpty()) surface_->grab().save(path);
}

void Radiation3DView::exportData()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Export Radiation Surface Data"),
        QStringLiteral("radiation-3d.csv"), tr("CSV file (*.csv)"));
    if (path.isEmpty()) return;
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    QString output = QStringLiteral(
        "frequency_mhz,theta_degrees,phi_degrees,selected_gain_dbi,total_gain_dbi,vertical_gain_dbi,horizontal_gain_dbi\n");
    for (const auto& sample : result_.radiation) {
        if (!sameFrequency(sample.frequencyMHz, settings.frequencyMHz)) continue;
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        output += QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n")
            .arg(sample.frequencyMHz, 0, 'g', 15).arg(sample.thetaDegrees, 0, 'g', 15)
            .arg(sample.phiDegrees, 0, 'g', 15).arg(gain, 0, 'g', 15)
            .arg(sample.totalGainDb, 0, 'g', 15).arg(sample.verticalGainDb, 0, 'g', 15)
            .arg(sample.horizontalGainDb, 0, 'g', 15);
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(output.toUtf8());
}

}
