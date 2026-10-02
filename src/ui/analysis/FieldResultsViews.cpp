#include "ui/analysis/FieldResultsViews.h"

#include "model/WireGeometry.h"
#include "ui/DisplayFormat.h"
#include "ui/analysis/SweepPlotsView.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
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

void selectOrAppendFrequency(QComboBox* control, double frequencyMHz)
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

void selectExistingFrequency(QComboBox* control, double frequencyMHz)
{
    for (auto index = 0; index < control->count(); ++index) {
        if (!sameFrequency(control->itemData(index).toDouble(), frequencyMHz)) continue;
        control->setCurrentIndex(index);
        return;
    }
}

void populateFrequencies(QComboBox* control, const std::vector<double>& values)
{
    control->clear();
    for (const auto value : values) {
        control->addItem(QStringLiteral("%1 MHz").arg(formatDecimal(value)), value);
    }
}

void populateRadiationComponents(QComboBox* component)
{
    component->addItem(QObject::tr("Total gain"), static_cast<int>(analysis::RadiationComponent::Total));
    component->addItem(QObject::tr("Vertical polarization"), static_cast<int>(analysis::RadiationComponent::Vertical));
    component->addItem(QObject::tr("Horizontal polarization"), static_cast<int>(analysis::RadiationComponent::Horizontal));
    component->addItem(QObject::tr("RHCP"), static_cast<int>(analysis::RadiationComponent::RightHandCircular));
    component->addItem(QObject::tr("LHCP"), static_cast<int>(analysis::RadiationComponent::LeftHandCircular));
}

void populateRadiationControls(QComboBox* component, QComboBox* scale, QComboBox* floor)
{
    populateRadiationComponents(component);
    scale->addItem(QObject::tr("Normalized dB"), static_cast<int>(analysis::RadiationScale::Normalized));
    scale->addItem(QObject::tr("Absolute dBi"), static_cast<int>(analysis::RadiationScale::Absolute));
    for (const auto value : {-20.0, -30.0, -40.0, -50.0, -60.0})
        floor->addItem(QObject::tr("%1 dB").arg(formatDecimal(value)), value);
    floor->setCurrentIndex(2);
}

struct PatternDatasetStats {
    int samples{};
    std::vector<double> theta;
    std::vector<double> phi;
};

auto patternDatasets(const analysis::AnalysisResult& result, double frequencyMHz)
    -> std::map<int, PatternDatasetStats>
{
    std::map<int, PatternDatasetStats> datasets;
    for (const auto& sample : result.radiation) {
        if (!sameFrequency(sample.frequencyMHz, frequencyMHz)) continue;
        auto& stats = datasets[sample.patternIndex];
        ++stats.samples;
        if (std::ranges::find(stats.theta, sample.thetaDegrees) == stats.theta.end())
            stats.theta.push_back(sample.thetaDegrees);
        if (std::ranges::find(stats.phi, sample.phiDegrees) == stats.phi.end())
            stats.phi.push_back(sample.phiDegrees);
    }
    return datasets;
}

void populateRadiationFrequencies(QComboBox* control,
    const analysis::AnalysisResult& result)
{
    control->clear();
    for (const auto frequencyMHz : frequencies(result.radiation)) {
        const auto count = patternDatasets(result, frequencyMHz).size();
        control->addItem(QObject::tr("%1 MHz — %2 pattern(s)")
            .arg(formatDecimal(frequencyMHz)).arg(count), frequencyMHz);
    }
}

auto patternDatasetLabel(int index, const PatternDatasetStats& stats) -> QString
{
    QString kind;
    if (stats.theta.size() == 1 && stats.phi.size() > 1) kind = QObject::tr("Horizontal cut");
    else if (stats.theta.size() > 1 && stats.phi.size() <= 2) kind = QObject::tr("Vertical cut");
    else if (stats.theta.size() > 1 && stats.phi.size() > 2) kind = QObject::tr("Full grid");
    else kind = QObject::tr("Partial grid");
    return QObject::tr("RP %1 — %2 (%3×%4)").arg(index + 1).arg(kind)
        .arg(stats.theta.size()).arg(stats.phi.size());
}

void populatePatternDatasets(QComboBox* control, const analysis::AnalysisResult& result,
    double frequencyMHz, bool preferSurface)
{
    const auto previous = control->currentData();
    const QSignalBlocker blocker(control);
    control->clear();
    const auto datasets = patternDatasets(result, frequencyMHz);
    auto preferredIndex = -1;
    auto preferredCoverage = std::size_t{};
    for (const auto& [index, stats] : datasets) {
        control->addItem(patternDatasetLabel(index, stats), index);
        const auto coverage = stats.theta.size() * stats.phi.size();
        if (preferSurface && stats.theta.size() > 1 && stats.phi.size() > 2
            && coverage > preferredCoverage) {
            preferredCoverage = coverage;
            preferredIndex = index;
        }
    }
    const auto previousIndex = control->findData(previous);
    if (previousIndex >= 0) control->setCurrentIndex(previousIndex);
    else if (preferredIndex >= 0) control->setCurrentIndex(control->findData(preferredIndex));
}

auto selectedPatternIndex(QComboBox* control) -> int
{
    return control->currentData().isValid() ? control->currentData().toInt() : -1;
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
        for (auto index = std::size_t{}; index < model::wirePathPointCount(wire); ++index) {
            const auto& point = model::wirePathPoint(wire, index);
            extent = std::max({extent, std::abs(point.x), std::abs(point.y), std::abs(point.z)});
        }
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

auto oppositeVerticalCutAngle(double thetaDegrees) -> double
{
    auto angle = 360.0 - thetaDegrees;
    while (angle >= 360.0) angle -= 360.0;
    while (angle < -360.0) angle += 360.0;
    return angle;
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
    using HoverTextCallback = std::function<void(const QString&)>;

    explicit RadiationPolarWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("radiationPolarPlot"));
        setMinimumSize(360, 360);
        setMouseTracking(true);
        setProperty("labelColorRole", static_cast<int>(QPalette::Text));
        setProperty("ringLabelBackground", true);
        setProperty("hoverSampleMode", QStringLiteral("nearest-nec-sample"));
    }
    void setSamples(std::vector<QPointF> samples)
    {
        samples_ = std::move(samples);
        setProperty("closedPattern", radiationAnglesCoverCircle(samples_));
        tracking_ = false;
        setProperty("hoverReadout", QString{});
        if (hoverTextCallback_) hoverTextCallback_({});
        update();
    }
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
    {
        settings_ = settings;
        update();
    }
    void setHoverTextCallback(HoverTextCallback callback)
    {
        hoverTextCallback_ = std::move(callback);
    }
protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        cursor_ = event->position();
        tracking_ = true;
        const auto text = nearestSampleText(cursor_);
        setProperty("hoverReadout", text);
        if (hoverTextCallback_) hoverTextCallback_(text);
        update();
    }
    void leaveEvent(QEvent*) override
    {
        tracking_ = false;
        setProperty("hoverReadout", QString{});
        if (hoverTextCallback_) hoverTextCallback_({});
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
        const auto gridColor = palette().color(QPalette::Mid);
        const auto labelColor = palette().color(QPalette::Text);
        const auto drawRingLabel = [&](const QPointF& position, const QString& text) {
            QRectF labelBounds(painter.fontMetrics().boundingRect(text));
            labelBounds.moveBottomLeft(position);
            const auto background = labelBounds.adjusted(-3.0, -1.0, 3.0, 1.0);
            painter.fillRect(background, palette().brush(QPalette::Base));
            painter.setPen(labelColor);
            painter.drawText(labelBounds, Qt::AlignLeft | Qt::AlignVCenter, text);
        };
        for (auto ring = 1; ring <= 4; ++ring) {
            const auto ringRadius = radius * ring / 4;
            painter.setPen(gridColor);
            painter.drawEllipse(center, ringRadius, ringRadius);
            const auto ringDb = innerDb + (outerDb - innerDb) * ring / 4.0;
            drawRingLabel(QPointF(center.x() + 5, center.y() - ringRadius - 2),
                tr("%1 dB").arg(formatDecimal(ringDb)));
        }
        for (auto angle = 0; angle < 360; angle += 30) {
            const auto radians = angle * std::numbers::pi / 180.0;
            painter.setPen(gridColor);
            painter.drawLine(center, center + QPointF(std::sin(radians), -std::cos(radians)) * radius);
            const auto labelPoint = center
                + QPointF(std::sin(radians), -std::cos(radians)) * (radius + 18.0);
            painter.setPen(labelColor);
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
        painter.drawText(8, 20, tr("Angle: degrees · Radial scale: %1 · Absolute peak: %2 dBi")
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
        }
    }
private:
    [[nodiscard]] auto nearestSampleText(const QPointF& position) const -> QString
    {
        if (samples_.empty()) return {};
        const QPointF center(width() / 2.0, height() / 2.0 + 8.0);
        auto cursorAngle = std::atan2(position.x() - center.x(),
                               center.y() - position.y())
            * 180.0 / std::numbers::pi;
        if (cursorAngle < 0.0) cursorAngle += 360.0;
        const auto nearest = std::ranges::min_element(samples_, {},
            [cursorAngle](const QPointF& sample) {
                const auto difference = std::abs(sample.x() - cursorAngle);
                return std::min(difference, 360.0 - difference);
            });
        const auto maxGain = std::ranges::max(samples_, {},
            [](const QPointF& point) { return point.y(); }).y();
        return tr("Nearest NEC sample · Angle %1° · Gain %2 dBi · Relative %3 dB")
            .arg(formatDecimal(nearest->x()), formatDecimal(nearest->y()),
                formatDecimal(nearest->y() - maxGain));
    }

    std::vector<QPointF> samples_;
    analysis::RadiationDisplaySettings settings_;
    QPointF cursor_;
    HoverTextCallback hoverTextCallback_;
    bool tracking_{};
};

class RadiationSurfaceWidget final : public QWidget {
public:
    using HoverTextCallback = std::function<void(const QString&)>;

    explicit RadiationSurfaceWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("radiation3DSurface"));
        setMinimumSize(420, 320);
        setMouseTracking(true);
        setProperty("probeMode", QStringLiteral("nearest-nec-sample"));
        setProperty("gainColorScale", true);
        setProperty("viewPitch", pitch_);
    }
    void setSamples(std::vector<analysis::RadiationSample> samples)
    {
        samples_ = std::move(samples);
        tracking_ = false;
        setProperty("hoverReadout", QString{});
        if (hoverTextCallback_) hoverTextCallback_({});
        update();
    }
    void setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
    { settings_ = settings; update(); }
    void setCurrents(std::vector<analysis::SegmentCurrentResult> currents) { currents_ = std::move(currents); update(); }
    void setModel(const model::AntennaModel& model) { model_ = model; update(); }
    void setLayerVisibility(bool antenna, bool currents, bool radiation)
    { showAntenna_ = antenna; showCurrents_ = currents; showRadiation_ = radiation; update(); }
    void setOverlayVisible(bool visible) { showOverlay_ = visible; update(); }
    void setHoverTextCallback(HoverTextCallback callback)
    { hoverTextCallback_ = std::move(callback); }
    void resetView()
    {
        yaw_ = -0.7;
        pitch_ = 0.45;
        zoom_ = 1.0;
        pan_ = {};
        setProperty("viewPitch", pitch_);
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
                pitch_ = std::clamp(pitch_ - delta.y() * 0.01, -1.4, 1.4);
                setProperty("viewPitch", pitch_);
            }
        }
        const auto text = nearestSampleText(cursor_);
        setProperty("hoverReadout", text);
        if (hoverTextCallback_) hoverTextCallback_(text);
        update();
    }
    void leaveEvent(QEvent*) override
    {
        tracking_ = false;
        setProperty("hoverReadout", QString{});
        if (hoverTextCallback_) hoverTextCallback_({});
        update();
    }
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
        const auto maximum = showRadiation_ ? maximumGain() : std::nullopt;
        const auto hasRadiation = maximum.has_value();
        const auto maxGain = maximum.value_or(0.0);
        const auto maximumCurrent = showCurrents_ ? currentMaximum() : std::nullopt;
        if (hasRadiation) drawRadiation(painter, scale, maxGain);
        const auto modelExtent = resultModelExtentFromOrigin(model_);
        const auto antennaScale = hasRadiation ? scale * 0.32 : scale * 1.7;
        drawAxes(painter, scale * 0.34);
        if (showAntenna_) drawAntenna(painter, antennaScale, modelExtent);
        if (maximumCurrent)
            drawCurrents(painter, antennaScale, modelExtent, *maximumCurrent);
        if (hasRadiation && tracking_) {
            if (const auto nearest = nearestProjectedSample(
                    cursor_, scale, gainReference(maxGain))) {
                painter.save();
                painter.setPen(QPen(QColor(25, 25, 25), 1));
                painter.setBrush(QColor(245, 245, 245));
                painter.drawEllipse(nearest->screen, 5, 5);
                painter.restore();
            }
        }
        painter.setPen(palette().color(QPalette::Text));
        if (showOverlay_) {
            auto status = tr("Drag to orbit · wheel to zoom · zoom %1×").arg(formatDecimal(zoom_));
            if (hasRadiation) status += tr(" · %1 peak %2 dBi")
                .arg(componentName(settings_.component), formatDecimal(maxGain));
            painter.drawText(10, 22, status + tr(" · Shift/middle-drag to pan"));
        }
        if (hasRadiation && showOverlay_) {
            const auto reference = gainReference(maxGain);
            const auto minimum = reference + settings_.floorDb;
            setProperty("colorScaleMinimum", minimum);
            setProperty("colorScaleMaximum", reference);
            drawColorLegend(painter, 42.0,
                settings_.scale == analysis::RadiationScale::Normalized
                    ? tr("Radiation: relative gain") : tr("Radiation: absolute gain"),
                settings_.scale == analysis::RadiationScale::Normalized
                    ? settings_.floorDb : minimum,
                settings_.scale == analysis::RadiationScale::Normalized ? 0.0 : reference,
                settings_.scale == analysis::RadiationScale::Normalized ? tr("dB") : tr("dBi"));
        }
        setProperty("currentColorScale", maximumCurrent.has_value());
        if (maximumCurrent && showOverlay_) {
            setProperty("currentColorScaleMinimum", 0.0);
            setProperty("currentColorScaleMaximum", *maximumCurrent);
            const auto legendTop = hasRadiation ? 100.0 : 42.0;
            setProperty("currentColorScaleTop", legendTop);
            drawColorLegend(painter, legendTop, tr("Current magnitude"), 0.0,
                *maximumCurrent, tr("A"));
        }
        if ((!showRadiation_ || samples_.empty()) && (!showCurrents_ || currents_.empty()) && model_.empty())
            painter.drawText(rect(), Qt::AlignCenter, tr("No 3D result data"));
    }
private:
    struct ProjectedRadiationSample {
        const analysis::RadiationSample* sample{};
        QPointF screen;
        double gain{};
    };

    [[nodiscard]] auto maximumGain() const -> std::optional<double>
    {
        auto maximum = -std::numeric_limits<double>::infinity();
        for (const auto& sample : samples_) {
            const auto gain = analysis::radiationGainDb(sample, settings_.component);
            if (std::isfinite(gain) && gain > -900.0) maximum = std::max(maximum, gain);
        }
        return std::isfinite(maximum) ? std::optional<double>{maximum} : std::nullopt;
    }
    [[nodiscard]] auto currentMaximum() const -> std::optional<double>
    {
        auto maximum = -std::numeric_limits<double>::infinity();
        for (const auto& current : currents_)
            if (std::isfinite(current.magnitude)) maximum = std::max(maximum, current.magnitude);
        return std::isfinite(maximum) ? std::optional<double>{maximum} : std::nullopt;
    }
    [[nodiscard]] auto gainReference(double maxGain) const -> double
    {
        return settings_.scale == analysis::RadiationScale::Normalized
            ? maxGain : std::ceil(maxGain / 5.0) * 5.0;
    }
    [[nodiscard]] auto gainRatio(double gain, double reference) const -> double
    {
        return std::clamp((gain - (reference + settings_.floorDb))
                / -settings_.floorDb,
            0.0, 1.0);
    }
    [[nodiscard]] static auto gainColor(double ratio) -> QColor
    {
        return QColor::fromHsvF((1.0 - std::clamp(ratio, 0.0, 1.0)) * 0.67,
            0.9, 0.95);
    }
    void drawColorLegend(QPainter& painter, double top, const QString& title,
        double minimum, double maximum, const QString& unit)
    {
        painter.save();
        const QRectF legend(10, top, 285, 50);
        painter.fillRect(legend, palette().brush(QPalette::AlternateBase));
        const QRectF colorBar(legend.left() + 8, legend.top() + 19, 185, 10);
        QLinearGradient gradient(colorBar.topLeft(), colorBar.topRight());
        for (auto index = 0; index <= 4; ++index) {
            const auto ratio = index / 4.0;
            gradient.setColorAt(ratio, gainColor(ratio));
        }
        painter.fillRect(colorBar, gradient);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(colorBar);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(legend.left() + 8, legend.top(), legend.width() - 16, 18),
            Qt::AlignLeft | Qt::AlignVCenter, title);
        painter.drawText(QRectF(colorBar.left(), colorBar.bottom() + 2, 90, 17),
            Qt::AlignLeft | Qt::AlignVCenter,
            tr("%1 %2").arg(formatDecimal(minimum), unit));
        painter.drawText(QRectF(colorBar.right() - 90, colorBar.bottom() + 2, 90, 17),
            Qt::AlignRight | Qt::AlignVCenter,
            tr("%1 %2").arg(formatDecimal(maximum), unit));
        painter.restore();
    }
    [[nodiscard]] auto samplePoint(const analysis::RadiationSample& sample,
        double gain, double reference) const -> model::Point3D
    {
        const auto theta = sample.thetaDegrees * std::numbers::pi / 180.0;
        const auto phi = sample.phiDegrees * std::numbers::pi / 180.0;
        const auto radial = std::pow(10.0,
            (std::max(gain, reference + settings_.floorDb) - reference) / 20.0);
        return {radial * std::sin(theta) * std::cos(phi),
            radial * std::sin(theta) * std::sin(phi), radial * std::cos(theta)};
    }
    [[nodiscard]] auto nearestSampleText(const QPointF& position) const -> QString
    {
        if (!showRadiation_ || samples_.empty()) return {};
        const auto maxGain = maximumGain();
        if (!maxGain) return {};
        const auto reference = gainReference(*maxGain);
        const auto scale = std::min(width(), height()) * 0.38 * zoom_;
        const auto nearest = nearestProjectedSample(position, scale, reference);
        if (!nearest) return {};
        return tr("Nearest NEC sample · θ %1° · φ %2° · Gain %3 dBi · Relative %4 dB")
            .arg(formatDecimal(nearest->sample->thetaDegrees),
                formatDecimal(nearest->sample->phiDegrees), formatDecimal(nearest->gain),
                formatDecimal(nearest->gain - *maxGain));
    }
    [[nodiscard]] auto nearestProjectedSample(const QPointF& position, double scale,
        double reference) const -> std::optional<ProjectedRadiationSample>
    {
        ProjectedRadiationSample nearest;
        auto nearestDistance = std::numeric_limits<double>::infinity();
        for (const auto& sample : samples_) {
            const auto gain = analysis::radiationGainDb(sample, settings_.component);
            if (!std::isfinite(gain) || gain <= -900.0) continue;
            const auto screen = project(samplePoint(sample, gain, reference), scale);
            const auto distance = QLineF(position, screen).length();
            if (distance < nearestDistance) {
                nearest = {&sample, screen, gain};
                nearestDistance = distance;
            }
        }
        return nearest.sample != nullptr
            ? std::optional<ProjectedRadiationSample>{nearest} : std::nullopt;
    }
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
        struct Vertex {
            double order{};
            QPointF screen;
            double colorRatio{};
        };
        std::map<double, std::vector<Vertex>> phiPaths;
        std::map<double, std::vector<Vertex>> thetaPaths;
        const auto reference = gainReference(maxGain);
        for (const auto& sample : samples_) {
            const auto gain = analysis::radiationGainDb(sample, settings_.component);
            if (!std::isfinite(gain) || gain <= -900.0) continue;
            const auto screen = project(samplePoint(sample, gain, reference), scale);
            const auto ratio = gainRatio(gain, reference);
            phiPaths[sample.phiDegrees].push_back({sample.thetaDegrees, screen, ratio});
            thetaPaths[sample.thetaDegrees].push_back({sample.phiDegrees, screen, ratio});
        }
        const auto drawPaths = [&painter](auto& paths, double width, bool close) {
            for (auto& [key, points] : paths) {
                Q_UNUSED(key);
                std::ranges::sort(points, {}, &Vertex::order);
                for (auto index = std::size_t{1}; index < points.size(); ++index) {
                    const auto ratio = (points[index - 1].colorRatio
                        + points[index].colorRatio) / 2.0;
                    painter.setPen(QPen(gainColor(ratio), width));
                    painter.drawLine(points[index - 1].screen, points[index].screen);
                }
                if (close && points.size() > 2) {
                    const auto ratio = (points.front().colorRatio
                        + points.back().colorRatio) / 2.0;
                    painter.setPen(QPen(gainColor(ratio), width));
                    painter.drawLine(points.back().screen, points.front().screen);
                }
            }
        };
        drawPaths(phiPaths, 2.0, false);
        const auto closeAzimuth = phiPaths.size() >= 3
            && (phiPaths.rbegin()->first - phiPaths.begin()->first) >= 270.0;
        drawPaths(thetaPaths, 1.0, closeAzimuth);
    }
    void drawAntenna(QPainter& painter, double scale, double extent)
    {
        if (model_.empty()) return;
        painter.setPen(QPen(QColor(245, 190, 45), 3));
        for (const auto& wire : model_.wires()) {
            for (auto index = std::size_t{1}; index < model::wirePathPointCount(wire); ++index) {
                painter.drawLine(
                    project(normalized(model::wirePathPoint(wire, index - 1), extent), scale),
                    project(normalized(model::wirePathPoint(wire, index), extent), scale));
            }
        }
    }
    void drawCurrents(QPainter& painter, double scale, double extent, double maximum)
    {
        painter.save();
        const analysis::SegmentCurrentResult* hovered{};
        QPointF hoveredStart;
        QPointF hoveredEnd;
        auto hoverDistance = 9.0;
        for (const auto& current : currents_) {
            const auto* wire = model_.wireByTag(current.wireTag);
            if (wire == nullptr || wire->segments <= 0 || current.segment < 1 || current.segment > wire->segments) continue;
            const auto endpoints = model::wireSegmentEndpoints(*wire, current.segment);
            if (!endpoints) continue;
            const auto start = project(normalized(endpoints->first, extent), scale);
            const auto end = project(normalized(endpoints->second, extent), scale);
            const auto ratio = current.magnitude / std::max(maximum, 1.0e-30);
            painter.setPen(QPen(QColor::fromHsvF((1.0-ratio)*0.67, 0.9, 0.95), 3.0 + 4.0*ratio));
            painter.drawLine(start, end);
            const auto distance = tracking_ ? pointToSegmentDistance(cursor_, start, end) : 10.0;
            if (distance < hoverDistance) {
                hoverDistance = distance; hovered = &current; hoveredStart = start; hoveredEnd = end;
            }
        }
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
        painter.restore();
    }
    model::AntennaModel model_;
    std::vector<analysis::RadiationSample> samples_;
    std::vector<analysis::SegmentCurrentResult> currents_;
    analysis::RadiationDisplaySettings settings_;
    QPointF last_;
    QPointF cursor_;
    QPointF pan_;
    HoverTextCallback hoverTextCallback_;
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
{ selectOrAppendFrequency(frequency_, frequencyMHz); refresh(); }
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
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation2DFrequency"));
    frequency_->setMinimumContentsLength(20);
    dataset_ = new QComboBox(this); dataset_->setObjectName(QStringLiteral("radiation2DDataset"));
    phi_ = new QComboBox(this); phi_->setObjectName(QStringLiteral("radiation2DCutPlane"));
    cutLabel_ = new QLabel(tr("Phi plane"), this);
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation2DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation2DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation2DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addRow(tr("Pattern frequency"), frequency_);
    form->addRow(tr("Pattern dataset"), dataset_);
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
    summary_->setWordWrap(true);
    hoverReadout_ = new QLabel(
        tr("Move the pointer over the pattern to inspect calculated NEC samples."), this);
    hoverReadout_->setObjectName(QStringLiteral("radiation2DHoverReadout"));
    hoverReadout_->setMinimumHeight(24);
    hoverReadout_->setAutoFillBackground(true);
    plot_ = new RadiationPolarWidget(this);
    plot_->setHoverTextCallback([this](const QString& text) {
        hoverReadout_->setText(text.isEmpty()
            ? tr("Move the pointer over the pattern to inspect calculated NEC samples.")
            : text);
    });
    layout->addLayout(form); layout->addLayout(navigation); layout->addWidget(summary_);
    layout->addWidget(hoverReadout_); layout->addWidget(plot_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refreshDatasets(); settingsChanged(); });
    connect(dataset_, &QComboBox::currentIndexChanged, this, [this] { refreshCutControls(); });
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
    populateRadiationFrequencies(frequency_, result);
    applyDisplaySettings(previous, frequency_, component_, scale_, floor_);
    refreshDatasets();
}
void RadiationPatternView::setSelectedFrequency(double frequencyMHz)
{
    const QSignalBlocker blocker(frequency_);
    selectExistingFrequency(frequency_, frequencyMHz);
    refreshDatasets();
}
void RadiationPatternView::setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
{
    updatingSettings_ = true;
    applyDisplaySettings(settings, frequency_, component_, scale_, floor_);
    updatingSettings_ = false;
    refreshDatasets();
}
void RadiationPatternView::setComponent(analysis::RadiationComponent component)
{
    const QSignalBlocker blocker(component_);
    const auto index = component_->findData(static_cast<int>(component));
    if (index >= 0) component_->setCurrentIndex(index);
    refresh();
}
void RadiationPatternView::setSettingsChangedCallback(SettingsChangedCallback callback)
{ settingsChangedCallback_ = std::move(callback); }
auto RadiationPatternView::availableCutPlanes(CutOrientation orientation) const -> std::vector<double>
{
    struct PlaneSamples {
        double plane{};
        std::vector<double> sweepAngles;
    };
    std::vector<PlaneSamples> samplesByPlane;
    for (const auto& value : result_.radiation) {
        if (!sameFrequency(value.frequencyMHz, selectedFrequency(frequency_))) continue;
        if (value.patternIndex != selectedPatternIndex(dataset_)) continue;
        auto plane = orientation == CutOrientation::Vertical ? value.phiDegrees : value.thetaDegrees;
        const auto sweepAngle = orientation == CutOrientation::Vertical
            ? value.thetaDegrees : value.phiDegrees;
        if (orientation == CutOrientation::Vertical) {
            plane = std::fmod(plane + 360.0, 360.0);
            if (plane >= 180.0) plane -= 180.0;
        } else if (sameFrequency(plane, 0.0) || sameFrequency(plane, 180.0)) {
            continue;
        }
        auto matchingPlane = std::ranges::find_if(samplesByPlane, [plane](const auto& existing) {
            return sameFrequency(existing.plane, plane);
        });
        if (matchingPlane == samplesByPlane.end()) {
            samplesByPlane.push_back({plane, {}});
            matchingPlane = std::prev(samplesByPlane.end());
        }
        if (std::ranges::none_of(matchingPlane->sweepAngles, [sweepAngle](double existing) {
                return std::abs(existing - sweepAngle) < 1.0e-9;
            })) {
            matchingPlane->sweepAngles.push_back(sweepAngle);
        }
    }
    std::vector<double> planes;
    for (const auto& samples : samplesByPlane) {
        if (samples.sweepAngles.size() >= 2) planes.push_back(samples.plane);
    }
    std::ranges::sort(planes);
    return planes;
}
void RadiationPatternView::refreshCutControls()
{
    const auto verticalAvailable = !availableCutPlanes(CutOrientation::Vertical).empty();
    const auto horizontalAvailable = !availableCutPlanes(CutOrientation::Horizontal).empty();
    if (orientation_ == CutOrientation::Vertical && !verticalAvailable && horizontalAvailable)
        orientation_ = CutOrientation::Horizontal;
    else if (orientation_ == CutOrientation::Horizontal && !horizontalAvailable && verticalAvailable)
        orientation_ = CutOrientation::Vertical;
    orientationButton_->setText(orientation_ == CutOrientation::Vertical
        ? tr("Vertical Cut") : tr("Horizontal Cut"));
    cutLabel_->setText(orientation_ == CutOrientation::Vertical
        ? tr("Phi plane") : tr("Theta angle"));
    orientationButton_->setEnabled(verticalAvailable && horizontalAvailable);
    if (verticalAvailable && horizontalAvailable)
        orientationButton_->setToolTip(tr("Switch between vertical and horizontal cuts."));
    else if (verticalAvailable || horizontalAvailable)
        orientationButton_->setToolTip(tr("The selected RP dataset supports only this cut orientation."));
    else
        orientationButton_->setToolTip(tr("The selected RP dataset has insufficient samples for a 2D cut."));
    refreshSelectors();
}
void RadiationPatternView::refreshSelectors()
{
    const auto previousValue = phi_->currentData().toDouble();
    phi_->clear();
    for (const auto plane : availableCutPlanes(orientation_))
        phi_->addItem(QStringLiteral("%1°").arg(formatDecimal(plane)), plane);
    const auto previousIndex = phi_->findData(previousValue); if (previousIndex >= 0) phi_->setCurrentIndex(previousIndex); refresh();
}
void RadiationPatternView::refreshDatasets()
{
    populatePatternDatasets(dataset_, result_, selectedFrequency(frequency_), false);
    refreshCutControls();
}
void RadiationPatternView::refresh()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    std::vector<analysis::RadiationCutPoint> cutSamples;
    const auto angle = phi_->currentData().toDouble();
    for (const auto& value : result_.radiation) {
        if (!sameFrequency(value.frequencyMHz, selectedFrequency(frequency_))) continue;
        if (value.patternIndex != selectedPatternIndex(dataset_)) continue;
        if (orientation_ == CutOrientation::Horizontal && value.thetaDegrees == angle)
            cutSamples.push_back({value.phiDegrees, value.thetaDegrees, value.phiDegrees,
                analysis::radiationGainDb(value, settings.component)});
        if (orientation_ == CutOrientation::Vertical) {
            const auto normalizedPhi = std::fmod(value.phiDegrees + 360.0, 360.0);
            if (std::abs(normalizedPhi - angle) < 1.0e-9)
                cutSamples.push_back({value.thetaDegrees, value.thetaDegrees, value.phiDegrees,
                    analysis::radiationGainDb(value, settings.component)});
            const auto opposite = std::fmod(angle + 180.0, 360.0);
            if (std::abs(normalizedPhi - opposite) < 1.0e-9)
                cutSamples.push_back({oppositeVerticalCutAngle(value.thetaDegrees),
                    value.thetaDegrees, value.phiDegrees,
                    analysis::radiationGainDb(value, settings.component)});
        }
    }
    std::erase_if(cutSamples, [](const auto& sample) {
        return !std::isfinite(sample.gainDb) || sample.gainDb <= -900.0;
    });
    std::vector<analysis::RadiationCutPoint> uniqueCutSamples;
    uniqueCutSamples.reserve(cutSamples.size());
    for (const auto& sample : cutSamples) {
        const auto duplicate = std::ranges::find_if(uniqueCutSamples, [&sample](const auto& existing) {
                return std::abs(std::remainder(
                    existing.angleDegrees - sample.angleDegrees, 360.0)) < 1.0e-9;
            });
        if (duplicate == uniqueCutSamples.end()) {
            uniqueCutSamples.push_back(sample);
        } else if (sample.gainDb > duplicate->gainDb) {
            *duplicate = sample;
        }
    }
    cutSamples = std::move(uniqueCutSamples);
    std::vector<QPointF> values;
    values.reserve(cutSamples.size());
    for (const auto& sample : cutSamples) values.emplace_back(sample.angleDegrees, sample.gainDb);
    std::ranges::sort(values, {}, [](const QPointF& point) { return point.x(); });
    const auto metrics = analysis::radiationCutMetrics(cutSamples, radiationAnglesCoverCircle(values));
    if (metrics.valid) {
        auto peakText = tr("%1 peak %2 dBi at %3°")
            .arg(componentName(settings.component), formatDecimal(metrics.peakGainDb),
                formatDecimal(metrics.peakAngleDegrees));
        if (metrics.tiedPeakAnglesDegrees.size() > 1) {
            QStringList tiedAngles;
            for (const auto tiedAngle : metrics.tiedPeakAnglesDegrees) {
                if (!sameFrequency(tiedAngle, metrics.peakAngleDegrees))
                    tiedAngles.append(QStringLiteral("%1°").arg(formatDecimal(tiedAngle)));
            }
            if (!tiedAngles.empty()) peakText += tr("; tied at %1").arg(tiedAngles.join(QStringLiteral(", ")));
        }
        const auto beamwidthText = metrics.interpolatedBeamwidthDegrees
            ? tr("3 dB HPBW %1° interpolated (%2° sampled)")
                .arg(formatDecimal(*metrics.interpolatedBeamwidthDegrees),
                    formatDecimal(*metrics.sampledBeamwidthDegrees))
            : tr("3 dB HPBW unavailable");
        const auto frontToBackText = metrics.frontToBackDb
            ? tr("F/B %1 dB").arg(formatDecimal(*metrics.frontToBackDb))
            : tr("F/B unavailable");
        summary_->setText(tr("%1 MHz · %2 · %3 · %4 · %5 · Left/Right changes angle · Space changes cut")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, peakText,
                beamwidthText, frontToBackText));
    } else {
        summary_->setText(tr("%1 MHz · No %2 samples for this cut · %3")
            .arg(formatDecimal(settings.frequencyMHz), componentName(settings.component), runContext_));
    }
    exportImageButton_->setEnabled(metrics.valid);
    exportDataButton_->setEnabled(metrics.valid);
    const auto patternIndex = selectedPatternIndex(dataset_);
    maxGainCutButton_->setEnabled(std::ranges::any_of(result_.radiation, [settings, patternIndex](const auto& sample) {
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return sameFrequency(sample.frequencyMHz, settings.frequencyMHz)
            && sample.patternIndex == patternIndex
            && std::isfinite(gain) && gain > -900.0;
    }));
    plot_->setDisplaySettings(settings);
    plot_->setSamples(std::move(values));
}
void RadiationPatternView::toggleOrientation()
{
    const auto next = orientation_ == CutOrientation::Vertical
        ? CutOrientation::Horizontal : CutOrientation::Vertical;
    if (availableCutPlanes(next).empty()) return;
    orientation_ = next;
    refreshCutControls();
}
void RadiationPatternView::stepAngle(int offset)
{
    if (phi_->count() == 0) return;
    phi_->setCurrentIndex((phi_->currentIndex() + offset + phi_->count()) % phi_->count());
}
void RadiationPatternView::showMaxGainCut()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    const auto patternIndex = selectedPatternIndex(dataset_);
    const auto peak = std::ranges::max_element(result_.radiation, {}, [settings, patternIndex](const auto& sample) {
        if (!sameFrequency(sample.frequencyMHz, settings.frequencyMHz)
            || sample.patternIndex != patternIndex)
            return -std::numeric_limits<double>::infinity();
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return std::isfinite(gain) && gain > -900.0
            ? gain : -std::numeric_limits<double>::infinity();
    });
    if (peak == result_.radiation.end()
        || !sameFrequency(peak->frequencyMHz, settings.frequencyMHz)) return;

    if (!availableCutPlanes(CutOrientation::Vertical).empty())
        orientation_ = CutOrientation::Vertical;
    else if (!availableCutPlanes(CutOrientation::Horizontal).empty())
        orientation_ = CutOrientation::Horizontal;
    else
        return;
    refreshCutControls();
    auto peakPlane = orientation_ == CutOrientation::Vertical
        ? std::fmod(peak->phiDegrees + 360.0, 360.0) : peak->thetaDegrees;
    if (orientation_ == CutOrientation::Vertical && peakPlane >= 180.0) peakPlane -= 180.0;
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
        if (sample.patternIndex != selectedPatternIndex(dataset_)) continue;
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
                angle = oppositeVerticalCutAngle(sample.thetaDegrees);
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
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation3DFrequency"));
    frequency_->setMinimumContentsLength(20);
    dataset_ = new QComboBox(this); dataset_->setObjectName(QStringLiteral("radiation3DDataset"));
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation3DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation3DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation3DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addRow(tr("Pattern frequency"), frequency_);
    form->addRow(tr("Pattern dataset"), dataset_);
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
    hoverReadout_ = new QLabel(
        tr("Move the pointer over the surface to inspect calculated NEC samples."), this);
    hoverReadout_->setObjectName(QStringLiteral("radiation3DHoverReadout"));
    hoverReadout_->setMinimumHeight(24);
    hoverReadout_->setAutoFillBackground(true);
    surface_ = new RadiationSurfaceWidget(this);
    surface_->setHoverTextCallback([this](const QString& text) {
        hoverReadout_->setText(text.isEmpty()
            ? tr("Move the pointer over the surface to inspect calculated NEC samples.")
            : text);
    });
    controlsLayout->addLayout(form);
    controlsLayout->addLayout(layers);
    layout->addWidget(controls_);
    layout->addWidget(summary_);
    layout->addWidget(hoverReadout_);
    layout->addWidget(surface_, 1);
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] { refreshDatasets(); settingsChanged(); });
    connect(dataset_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
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
    const QSignalBlocker blocker(frequency_);
    populateRadiationFrequencies(frequency_, result);
    applyDisplaySettings(previous, frequency_, component_, scale_, floor_);
    refreshDatasets();
}
void Radiation3DView::setModel(const model::AntennaModel& model)
{ model_ = model; surface_->setModel(model_); }
void Radiation3DView::setSelectedFrequency(double frequencyMHz)
{
    const QSignalBlocker blocker(frequency_);
    selectExistingFrequency(frequency_, frequencyMHz);
    refreshDatasets();
}
void Radiation3DView::setDisplaySettings(const analysis::RadiationDisplaySettings& settings)
{
    updatingSettings_ = true;
    applyDisplaySettings(settings, frequency_, component_, scale_, floor_);
    updatingSettings_ = false;
    refreshDatasets();
}
void Radiation3DView::setComponent(analysis::RadiationComponent component)
{
    const QSignalBlocker blocker(component_);
    const auto index = component_->findData(static_cast<int>(component));
    if (index >= 0) component_->setCurrentIndex(index);
    refresh();
}
void Radiation3DView::setSettingsChangedCallback(SettingsChangedCallback callback)
{ settingsChangedCallback_ = std::move(callback); }
void Radiation3DView::setOverviewMode(bool enabled)
{
    controls_->setVisible(!enabled);
    summary_->setVisible(!enabled);
    hoverReadout_->setVisible(!enabled);
    surface_->setOverlayVisible(!enabled);
    layout()->setContentsMargins(enabled ? 0 : 11, enabled ? 0 : 11,
        enabled ? 0 : 11, enabled ? 0 : 11);
}
void Radiation3DView::refresh()
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    std::vector<analysis::RadiationSample> radiation;
    std::vector<analysis::SegmentCurrentResult> currents;
    for (const auto& value : result_.radiation)
        if (sameFrequency(value.frequencyMHz, selectedFrequency(frequency_))
            && value.patternIndex == selectedPatternIndex(dataset_)) radiation.push_back(value);
    for (const auto& value : result_.currents) if (value.frequencyMHz == selectedFrequency(frequency_)) currents.push_back(value);
    const auto metrics = analysis::radiationMetrics(radiation, settings.component);
    std::vector<double> planes;
    for (const auto& sample : radiation)
        if (std::ranges::find(planes, sample.phiDegrees) == planes.end()) planes.push_back(sample.phiDegrees);
    radiationControl_->setToolTip(planes.size() < 3
        ? tr("This dataset has partial radiation coverage. Select a Full grid dataset for a complete surface.")
        : QString{});
    if (metrics.valid) {
        const auto frontToBackText = metrics.frontToBackDb
            ? tr("F/B %1 dB").arg(formatDecimal(*metrics.frontToBackDb))
            : tr("F/B unavailable");
        auto text = tr("%1 MHz · %2 · %3 peak %4 dBi at θ %5°, φ %6° · %7 · %8 phi plane(s)")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, componentName(settings.component))
            .arg(formatDecimal(metrics.peakGainDb), formatDecimal(metrics.peakThetaDegrees),
                formatDecimal(metrics.peakPhiDegrees), frontToBackText)
            .arg(planes.size());
        if (metrics.tiedPeaks.size() > 1)
            text += tr(" · %1 tied peak directions").arg(metrics.tiedPeaks.size());
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
void Radiation3DView::refreshDatasets()
{
    populatePatternDatasets(dataset_, result_, selectedFrequency(frequency_), true);
    refresh();
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
        if (sample.patternIndex != selectedPatternIndex(dataset_)) continue;
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

RadiationPerformanceView::RadiationPerformanceView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("radiationPerformanceView"));
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    component_ = new QComboBox(this);
    component_->setObjectName(QStringLiteral("radiationPerformanceComponent"));
    populateRadiationComponents(component_);
    forwardTheta_ = new QDoubleSpinBox(this);
    forwardTheta_->setObjectName(QStringLiteral("radiationPerformanceTheta"));
    forwardTheta_->setRange(-360.0, 360.0);
    forwardTheta_->setDecimals(DisplayDecimalPlaces);
    forwardTheta_->setValue(90.0);
    forwardTheta_->setSuffix(QStringLiteral("°"));
    forwardPhi_ = new QDoubleSpinBox(this);
    forwardPhi_->setObjectName(QStringLiteral("radiationPerformancePhi"));
    forwardPhi_->setRange(-360.0, 720.0);
    forwardPhi_->setDecimals(DisplayDecimalPlaces);
    forwardPhi_->setSuffix(QStringLiteral("°"));
    const auto directionTip = tr(
        "Physical forward direction used for gain, F/B, and F/R. Exact NEC samples "
        "must exist at the requested direction; values are not interpolated.");
    forwardTheta_->setToolTip(directionTip);
    forwardPhi_->setToolTip(directionTip);
    controls->addWidget(new QLabel(tr("Component"), this));
    controls->addWidget(component_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Forward θ"), this));
    controls->addWidget(forwardTheta_);
    controls->addWidget(new QLabel(tr("Forward φ"), this));
    controls->addWidget(forwardPhi_);
    controls->addStretch();
    summary_ = new QLabel(tr(
        "Load radiation results to inspect directional performance across frequency."), this);
    summary_->setObjectName(QStringLiteral("radiationPerformanceSummary"));
    summary_->setWordWrap(true);
    plot_ = new DirectionalMetricsView(this);
    plot_->setObjectName(QStringLiteral("radiationPerformancePlot"));
    layout->addLayout(controls);
    layout->addWidget(summary_);
    layout->addWidget(plot_, 1);
    connect(component_, &QComboBox::currentIndexChanged, this, [this] {
        refresh();
        if (componentChangedCallback_) {
            componentChangedCallback_(static_cast<analysis::RadiationComponent>(
                component_->currentData().toInt()));
        }
    });
    connect(forwardTheta_, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
    connect(forwardPhi_, &QDoubleSpinBox::valueChanged, this, [this] { refresh(); });
}

void RadiationPerformanceView::setResults(
    const analysis::AnalysisResult& result, const QString& runDirectory)
{
    result_ = result;
    runContext_ = runDirectory;
    refresh();
}

void RadiationPerformanceView::setSelectedFrequency(double frequencyMHz)
{
    selectedFrequencyMHz_ = frequencyMHz;
    plot_->setSelectedFrequency(frequencyMHz);
}

void RadiationPerformanceView::setComponent(analysis::RadiationComponent component)
{
    const QSignalBlocker blocker(component_);
    const auto index = component_->findData(static_cast<int>(component));
    if (index >= 0) component_->setCurrentIndex(index);
    refresh();
}

void RadiationPerformanceView::setComponentChangedCallback(
    ComponentChangedCallback callback)
{
    componentChangedCallback_ = std::move(callback);
}

void RadiationPerformanceView::refresh()
{
    const auto component = static_cast<analysis::RadiationComponent>(
        component_->currentData().toInt());
    const auto metrics = analysis::radiationFrequencyMetrics(result_.radiation, component,
        forwardTheta_->value(), forwardPhi_->value());
    std::vector<DirectionalPlotPoint> points;
    points.reserve(metrics.size());
    auto gainCount = std::size_t{};
    auto frontToBackCount = std::size_t{};
    auto frontToRearCount = std::size_t{};
    for (const auto& metric : metrics) {
        points.push_back({metric.frequencyMHz, metric.forwardGainDb,
            metric.frontToBackDb, metric.frontToRearDb});
        gainCount += metric.forwardGainDb.has_value();
        frontToBackCount += metric.frontToBackDb.has_value();
        frontToRearCount += metric.frontToRearDb.has_value();
    }
    plot_->setMetrics(points);
    if (selectedFrequencyMHz_) plot_->setSelectedFrequency(*selectedFrequencyMHz_);
    if (metrics.empty()) {
        summary_->setText(tr("No radiation samples are available · %1").arg(runContext_));
        return;
    }
    summary_->setText(tr(
        "Forward θ %1°, φ %2° · %3 · Available frequencies: Gain %4/%7, "
        "F/B %5/%7, F/R %6/%7 · Missing exact directions remain unavailable · %8")
        .arg(formatDecimal(forwardTheta_->value()), formatDecimal(forwardPhi_->value()),
            componentName(component))
        .arg(gainCount).arg(frontToBackCount).arg(frontToRearCount).arg(metrics.size())
        .arg(runContext_));
}

}
