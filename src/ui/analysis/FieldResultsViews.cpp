#include "ui/analysis/FieldResultsViews.h"

#include "analysis/FrequencyComparison.h"
#include "analysis/SegmentCurrentDisplay.h"
#include "model/WireGeometry.h"
#include "ui/DisplayFormat.h"
#include "ui/analysis/CurrentAntennaView.h"
#include "ui/analysis/SweepPlotsView.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QLineF>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
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
#include <span>
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

void selectOrAppendFrequency(QComboBox* control, double frequencyMHz)
{
    for (auto index = 0; index < control->count(); ++index) {
        if (nearlyEqual(control->itemData(index).toDouble(), frequencyMHz)) {
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
        if (!nearlyEqual(control->itemData(index).toDouble(), frequencyMHz)) continue;
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
        if (!nearlyEqual(sample.frequencyMHz, frequencyMHz)) continue;
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
    if (stats.theta.size() == 1 && stats.phi.size() > 1) kind = QObject::tr("Azimuth cut");
    else if (stats.theta.size() > 1 && stats.phi.size() <= 2) kind = QObject::tr("Elevation cut");
    else if (stats.theta.size() > 1 && stats.phi.size() > 2) kind = QObject::tr("Full grid");
    else kind = QObject::tr("Partial grid");
    return QObject::tr("RP %1 — %2 (%3×%4)").arg(index + 1).arg(kind)
        .arg(stats.theta.size()).arg(stats.phi.size());
}

auto samePatternGrid(const PatternDatasetStats& first,
    const PatternDatasetStats& second) -> bool
{
    const auto sameValues = [](const auto& left, const auto& right) {
        return left.size() == right.size()
            && std::ranges::all_of(left, [&right](double value) {
                return std::ranges::any_of(right, [value](double candidate) {
                    return nearlyEqual(value, candidate);
                });
            });
    };
    return sameValues(first.theta, second.theta) && sameValues(first.phi, second.phi);
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
    for (const auto& patch : model.surfacePatches()) {
        for (const auto& point : patch.corners)
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

auto oppositeVerticalCutAngle(double thetaDegrees) -> double
{
    auto angle = 360.0 - thetaDegrees;
    while (angle >= 360.0) angle -= 360.0;
    while (angle < -360.0) angle += 360.0;
    return angle;
}

auto radiationCutSamples(const analysis::AnalysisResult& result, double frequencyMHz,
    int patternIndex, bool vertical, double plane,
    analysis::RadiationComponent component) -> std::vector<analysis::RadiationCutPoint>
{
    std::vector<analysis::RadiationCutPoint> samples;
    for (const auto& value : result.radiation) {
        if (!nearlyEqual(value.frequencyMHz, frequencyMHz)
            || value.patternIndex != patternIndex) continue;
        if (!vertical && nearlyEqual(value.thetaDegrees, plane)) {
            samples.push_back({value.phiDegrees, value.thetaDegrees, value.phiDegrees,
                analysis::radiationGainDb(value, component)});
        }
        if (vertical) {
            const auto normalizedPhi = std::fmod(value.phiDegrees + 360.0, 360.0);
            if (nearlyEqual(normalizedPhi, plane)) {
                samples.push_back({value.thetaDegrees, value.thetaDegrees, value.phiDegrees,
                    analysis::radiationGainDb(value, component)});
            }
            const auto opposite = std::fmod(plane + 180.0, 360.0);
            if (nearlyEqual(normalizedPhi, opposite)) {
                samples.push_back({oppositeVerticalCutAngle(value.thetaDegrees),
                    value.thetaDegrees, value.phiDegrees,
                    analysis::radiationGainDb(value, component)});
            }
        }
    }
    std::erase_if(samples, [](const auto& sample) {
        return !std::isfinite(sample.gainDb) || sample.gainDb <= -900.0;
    });
    std::vector<analysis::RadiationCutPoint> unique;
    unique.reserve(samples.size());
    for (const auto& sample : samples) {
        const auto duplicate = std::ranges::find_if(unique, [&sample](const auto& existing) {
            return std::abs(std::remainder(
                existing.angleDegrees - sample.angleDegrees, 360.0)) < 1.0e-9;
        });
        if (duplicate == unique.end()) unique.push_back(sample);
        else if (sample.gainDb > duplicate->gainDb) *duplicate = sample;
    }
    return unique;
}

auto findRadiationSample(const analysis::AnalysisResult& result, double frequencyMHz,
    int patternIndex, double thetaDegrees, double phiDegrees) -> const analysis::RadiationSample*
{
    const auto match = std::ranges::find_if(result.radiation, [&](const auto& sample) {
        return nearlyEqual(sample.frequencyMHz, frequencyMHz)
            && sample.patternIndex == patternIndex
            && nearlyEqual(sample.thetaDegrees, thetaDegrees)
            && nearlyEqual(sample.phiDegrees, phiDegrees);
    });
    return match == result.radiation.end() ? nullptr : &*match;
}

auto plotPoints(const std::vector<analysis::RadiationCutPoint>& samples) -> std::vector<QPointF>
{
    std::vector<QPointF> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) values.emplace_back(sample.angleDegrees, sample.gainDb);
    std::ranges::sort(values, {}, [](const QPointF& point) { return point.x(); });
    return values;
}

auto sameAngularGrid(const std::vector<QPointF>& first, const std::vector<QPointF>& second) -> bool
{
    return first.size() == second.size()
        && std::ranges::equal(first, second, [](const auto& left, const auto& right) {
            return nearlyEqual(left.x(), right.x());
        });
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

    // Group by physical wire; wires that share a tag (tag 0 is common) stay separate.
    const analysis::WireSegmentIndex index(model);
    std::map<std::size_t, std::vector<std::pair<int, analysis::SegmentCurrentResult>>> samplesByWire;
    std::map<int, CurrentPlotPath> unplacedByTag;
    for (const auto& sample : samples) {
        if (const auto location = index.locate(sample))
            samplesByWire[location->wireIndex].emplace_back(location->segment, sample);
        else
            unplacedByTag[sample.wireTag].push_back(sample);
    }

    std::vector<WireSeries> series;
    series.reserve(samplesByWire.size() + unplacedByTag.size());
    for (auto& [wireIndex, located] : samplesByWire) {
        std::ranges::sort(located, {}, &std::pair<int, analysis::SegmentCurrentResult>::first);
        CurrentPlotPath wireSamples;
        for (auto& entry : located) wireSamples.push_back(std::move(entry.second));
        const auto& wire = model.wires()[wireIndex];
        series.push_back({std::move(wireSamples), wire.start, wire.end, true});
    }
    for (auto& [tag, wireSamples] : unplacedByTag) {
        std::ranges::sort(wireSamples, {}, &analysis::SegmentCurrentResult::segment);
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
    using ForwardDirectionCallback = std::function<void(double, double)>;

    explicit RadiationPolarWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("radiationPolarPlot"));
        setMinimumSize(360, 360);
        setMouseTracking(true);
        setProperty("labelColorRole", static_cast<int>(QPalette::Text));
        setProperty("ringLabelBackground", true);
        setProperty("hoverSampleMode", QStringLiteral("nearest-nec-sample"));
    }
    void setSamples(std::vector<analysis::RadiationCutPoint> cut)
    {
        std::ranges::sort(cut, {}, &analysis::RadiationCutPoint::angleDegrees);
        samples_ = plotPoints(cut);
        cut_ = std::move(cut);
        setProperty("closedPattern", radiationAnglesCoverCircle(samples_));
        tracking_ = false;
        setProperty("hoverReadout", QString{});
        if (hoverTextCallback_) hoverTextCallback_({});
        update();
    }
    void setReferenceSamples(std::vector<QPointF> samples, QString label)
    {
        referenceSamples_ = std::move(samples);
        referenceLabel_ = std::move(label);
        setProperty("referenceSampleCount", static_cast<int>(referenceSamples_.size()));
        setProperty("referenceVisible", !referenceSamples_.empty());
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
    void setForwardDirectionCallback(ForwardDirectionCallback callback)
    {
        forwardDirectionCallback_ = std::move(callback);
    }
    // A vertical cut plots NEC theta around the circle through the plane at `plane` phi;
    // a horizontal cut plots phi at `plane` theta.
    void setCut(bool vertical, double plane)
    {
        vertical_ = vertical;
        plane_ = plane;
        setProperty("cutName", cutName());
        update();
    }
    [[nodiscard]] auto cutName() const -> QString
    {
        return fromCore(vertical_ ? analysis::elevationCutName(plane_)
                                  : analysis::azimuthCutName(plane_));
    }
protected:
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (cut_.empty() || !forwardDirectionCallback_) return;
        const auto* nearest = nearestCutPoint(event->pos());
        const auto peak = std::ranges::max_element(cut_, {}, &analysis::RadiationCutPoint::gainDb);
        QMenu menu(this);
        const auto* useNearest = menu.addAction(tr("Use This Sample as Forward Direction (%1)")
            .arg(fromCore(analysis::formatDirection(nearest->thetaDegrees, nearest->phiDegrees))));
        const auto* usePeak = menu.addAction(tr("Use Peak in This Cut as Forward Direction (%1)")
            .arg(fromCore(analysis::formatDirection(peak->thetaDegrees, peak->phiDegrees))));
        const auto* chosen = menu.exec(event->globalPos());
        if (chosen == useNearest)
            forwardDirectionCallback_(nearest->thetaDegrees, nearest->phiDegrees);
        else if (chosen == usePeak)
            forwardDirectionCallback_(peak->thetaDegrees, peak->phiDegrees);
    }
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
        const auto plot = frame();
        const auto& center = plot.center;
        const auto radius = plot.radius;
        setProperty("upperHalf", plot.upperHalf);
        auto maxGain = 0.0;
        if (!samples_.empty()) {
            maxGain = std::ranges::max(samples_, {},
                [](const QPointF& point) { return point.y(); }).y();
        }
        auto referenceMaxGain = 0.0;
        if (!referenceSamples_.empty()) {
            referenceMaxGain = std::ranges::max(referenceSamples_, {},
                [](const QPointF& point) { return point.y(); }).y();
        }
        const auto scaleMaximum = settings_.scale == analysis::RadiationScale::Absolute
            ? std::max(maxGain, referenceMaxGain) : maxGain;
        const auto outerDb = settings_.scale == analysis::RadiationScale::Normalized
            ? 0.0 : std::ceil(scaleMaximum / 5.0) * 5.0;
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
            if (plot.upperHalf) {
                painter.drawArc(QRectF(center.x() - ringRadius, center.y() - ringRadius,
                    2.0 * ringRadius, 2.0 * ringRadius), 0, 180 * 16);
            } else {
                painter.drawEllipse(center, ringRadius, ringRadius);
            }
            const auto ringDb = innerDb + (outerDb - innerDb) * ring / 4.0;
            drawRingLabel(QPointF(center.x() + 5, center.y() - ringRadius - 2),
                settings_.scale == analysis::RadiationScale::Normalized
                    ? tr("%1 dB").arg(formatDecimal(ringDb))
                    : tr("%1 dBi").arg(formatDecimal(ringDb)));
        }
        for (auto angle = 0; angle < 360; angle += 30) {
            if (plot.upperHalf && angle > 90 && angle < 270) continue;
            painter.setPen(gridColor);
            painter.drawLine(center, screenPoint(plot, angle, radius));
            const auto labelPoint = screenPoint(plot, angle, radius + 18.0);
            painter.setPen(labelColor);
            // Elevation cuts are labelled in elevation: the horizon is level and the zenith is up.
            const auto label = vertical_
                ? fromCore(analysis::formatAngle(angle <= 180 ? 90.0 - angle : angle - 270.0))
                : tr("%1°").arg(angle);
            painter.drawText(QRectF(labelPoint.x() - 22, labelPoint.y() - 9, 44, 18),
                Qt::AlignCenter, label);
        }
        if (vertical_) {
            painter.setPen(labelColor);
            const auto forwardSide = tr("Az %1 →").arg(
                fromCore(analysis::formatAngle(analysis::patternDirection(90.0, plane_).azimuthDegrees)));
            const auto backSide = tr("← Az %1").arg(fromCore(analysis::formatAngle(
                analysis::patternDirection(90.0, plane_ + 180.0).azimuthDegrees)));
            painter.drawText(QRectF(center.x() + radius - 90, center.y() + 4, 90, 18),
                Qt::AlignRight | Qt::AlignVCenter, forwardSide);
            painter.drawText(QRectF(center.x() - radius, center.y() + 4, 90, 18),
                Qt::AlignLeft | Qt::AlignVCenter, backSide);
        }
        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
        painter.drawLine(center - QPointF(0, radius),
            plot.upperHalf ? center : center + QPointF(0, radius));
        if (samples_.empty()) {
            painter.drawText(rect(), Qt::AlignCenter, tr("No radiation samples"));
            return;
        }
        const auto drawPattern = [&](const std::vector<QPointF>& samples, double peak,
                                     const QPen& pen) {
            if (samples.empty()) return;
            QPainterPath path;
            auto started = false;
            for (const auto& sample : samples) {
                const auto displayedDb = settings_.scale == analysis::RadiationScale::Normalized
                    ? sample.y() - peak : sample.y();
                const auto normalized = std::clamp(
                    (displayedDb - innerDb) / (outerDb - innerDb), 0.0, 1.0);
                const auto point = screenPoint(plot, sample.x(), radius * normalized);
                started ? path.lineTo(point) : path.moveTo(point);
                started = true;
            }
            if (radiationAnglesCoverCircle(samples)) path.closeSubpath();
            painter.setPen(pen);
            painter.drawPath(path);
        };
        drawPattern(referenceSamples_, referenceMaxGain,
            QPen(QColor(65, 105, 165, 145), 2, Qt::DashLine));
        drawPattern(samples_, maxGain, QPen(QColor(215, 70, 65), 2));
        painter.setPen(palette().color(QPalette::Text));
        const auto reference = referenceText(maxGain);
        setProperty("referenceLevel", reference);
        painter.drawText(8, 20, painter.fontMetrics().elidedText(
            tr("%1 · Peak in this cut %2 dBi · %3")
                .arg(cutName(), formatDecimal(maxGain), reference),
            Qt::ElideRight, width() - 16));
        if (!referenceSamples_.empty()) {
            // Each pattern is normalized to its own peak.
            const auto overlayText = settings_.scale == analysis::RadiationScale::Normalized
                ? tr("Overlay — %1 · %2").arg(referenceLabel_, referenceText(referenceMaxGain))
                : tr("Overlay — %1").arg(referenceLabel_);
            const auto label = painter.fontMetrics().elidedText(
                overlayText, Qt::ElideRight, std::max(80, width() / 2));
            const auto left = width() - painter.fontMetrics().horizontalAdvance(label) - 34;
            painter.setPen(QPen(QColor(65, 105, 165, 175), 2, Qt::DashLine));
            painter.drawLine(QPointF(left, 36), QPointF(left + 20, 36));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(left + 25, 40, label);
        }
        if (tracking_) {
            const auto nearest = nearestSample(cursor_);
            const auto displayedDb = settings_.scale == analysis::RadiationScale::Normalized
                ? nearest->y() - maxGain : nearest->y();
            const auto normalized = std::clamp((displayedDb - innerDb) / (outerDb - innerDb), 0.0, 1.0);
            const auto marker = screenPoint(plot, nearest->x(), radius * normalized);
            painter.setPen(QPen(QColor(35, 115, 220), 1));
            painter.setBrush(QColor(35, 115, 220));
            painter.drawEllipse(marker, 4, 4);
        }
    }
private:
    struct PlotFrame {
        QPointF center;
        double radius{};
        bool upperHalf{};
    };

    // An elevation cut with nothing below the horizon (a model over ground) is drawn as the
    // upper semicircle, as other amateur modeling programs do.
    [[nodiscard]] auto frame() const -> PlotFrame
    {
        const auto aboveHorizon = vertical_ && !cut_.empty()
            && std::ranges::all_of(cut_, [](const auto& point) {
                   return analysis::patternDirection(point.thetaDegrees, point.phiDegrees)
                       .elevationDegrees >= -1.0e-6;
               });
        if (aboveHorizon) {
            const auto radius = std::max(1.0, std::min(width() / 2.0 - 55.0, height() - 115.0));
            return {QPointF(width() / 2.0, std::min(height() - 32.0, 80.0 + radius)), radius, true};
        }
        return {QPointF(width() / 2.0, height() / 2.0 + 8.0),
            std::max(1.0, std::min(width(), height()) / 2.0 - 55.0), false};
    }
    // Elevation cuts put the zenith up and the plane's azimuth on the right, turning
    // clockwise in NEC theta. Azimuth cuts are seen from above: Az 0° (+X) on the right,
    // counterclockwise toward +Y.
    [[nodiscard]] auto screenPoint(const PlotFrame& plot, double angleDegrees,
        double distance) const -> QPointF
    {
        const auto radians = angleDegrees * std::numbers::pi / 180.0;
        return plot.center + (vertical_ ? QPointF(std::sin(radians), -std::cos(radians))
                                        : QPointF(std::cos(radians), -std::sin(radians)))
            * distance;
    }
    [[nodiscard]] auto angleAt(const QPointF& position) const -> double
    {
        const auto center = frame().center;
        const auto right = position.x() - center.x();
        const auto up = center.y() - position.y();
        auto angle = (vertical_ ? std::atan2(right, up) : std::atan2(up, right))
            * 180.0 / std::numbers::pi;
        if (angle < 0.0) angle += 360.0;
        return angle;
    }
    [[nodiscard]] static auto angularDistance(double first, double second) -> double
    {
        const auto difference = std::abs(std::remainder(first - second, 360.0));
        return std::min(difference, 360.0 - difference);
    }
    [[nodiscard]] auto nearestSample(const QPointF& position) const
        -> std::vector<QPointF>::const_iterator
    {
        const auto cursorAngle = angleAt(position);
        return std::ranges::min_element(samples_, {}, [cursorAngle](const QPointF& sample) {
            return angularDistance(sample.x(), cursorAngle);
        });
    }
    [[nodiscard]] auto nearestSampleText(const QPointF& position) const -> QString
    {
        if (samples_.empty()) return {};
        const auto nearest = nearestSample(position);
        const auto maxGain = std::ranges::max(samples_, {},
            [](const QPointF& point) { return point.y(); }).y();
        const auto& direction = cut_[static_cast<std::size_t>(
            std::distance(samples_.begin(), nearest))];
        auto text = tr("Nearest NEC sample · %1 (NEC %2) · Gain %3 dBi · Relative %4 dB")
            .arg(fromCore(analysis::formatDirection(direction.thetaDegrees, direction.phiDegrees)),
                fromCore(analysis::formatNecDirection(direction.thetaDegrees, direction.phiDegrees)),
                formatDecimal(nearest->y()), formatDecimal(nearest->y() - maxGain));
        if (settings_.scale == analysis::RadiationScale::Normalized)
            text += tr(" · %1").arg(referenceText(maxGain));
        if (forwardDirectionCallback_) text += tr(" · Right-click to use as forward direction");
        return text;
    }
    [[nodiscard]] auto referenceText(double maxGain) const -> QString
    {
        return settings_.scale == analysis::RadiationScale::Normalized
            ? fromCore(analysis::referenceLevelText(maxGain))
            : tr("Radial scale dBi");
    }
    [[nodiscard]] auto nearestCutPoint(const QPointF& position) const
        -> const analysis::RadiationCutPoint*
    {
        return &cut_[static_cast<std::size_t>(
            std::distance(samples_.begin(), nearestSample(position)))];
    }

    std::vector<QPointF> samples_;
    std::vector<analysis::RadiationCutPoint> cut_;
    std::vector<QPointF> referenceSamples_;
    QString referenceLabel_;
    analysis::RadiationDisplaySettings settings_;
    QPointF cursor_;
    HoverTextCallback hoverTextCallback_;
    ForwardDirectionCallback forwardDirectionCallback_;
    double plane_{};
    bool vertical_{true};
    bool tracking_{};
};

class RadiationSurfaceWidget final : public QWidget {
public:
    using HoverTextCallback = std::function<void(const QString&)>;
    using ForwardDirectionCallback = std::function<void(double, double)>;

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
    void setReferenceSamples(std::vector<analysis::RadiationSample> samples, QString label)
    {
        referenceSamples_ = std::move(samples);
        referenceLabel_ = std::move(label);
        setProperty("referenceSampleCount", static_cast<int>(referenceSamples_.size()));
        setProperty("referenceVisible", !referenceSamples_.empty());
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
    void setForwardDirectionCallback(ForwardDirectionCallback callback)
    { forwardDirectionCallback_ = std::move(callback); }
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
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        const auto maxGain = showRadiation_ ? maximumGain(samples_) : std::nullopt;
        if (!maxGain || !forwardDirectionCallback_) return;
        const auto scale = std::min(width(), height()) * 0.38 * zoom_;
        const auto nearest = nearestProjectedSample(event->pos(), scale, gainReference(*maxGain));
        const auto metrics = analysis::radiationMetrics(samples_, settings_.component);
        if (!nearest || !metrics.valid) return;
        QMenu menu(this);
        const auto* useNearest = menu.addAction(tr("Use This Sample as Forward Direction (%1)")
            .arg(fromCore(analysis::formatDirection(
                nearest->sample->thetaDegrees, nearest->sample->phiDegrees))));
        const auto* usePeak = menu.addAction(tr("Use Dataset Peak as Forward Direction (%1)")
            .arg(fromCore(analysis::formatDirection(
                metrics.peakThetaDegrees, metrics.peakPhiDegrees))));
        const auto* chosen = menu.exec(event->globalPos());
        if (chosen == useNearest)
            forwardDirectionCallback_(nearest->sample->thetaDegrees, nearest->sample->phiDegrees);
        else if (chosen == usePeak)
            forwardDirectionCallback_(metrics.peakThetaDegrees, metrics.peakPhiDegrees);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::RightButton) return;
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
        const auto maximum = showRadiation_ ? maximumGain(samples_) : std::nullopt;
        const auto referenceMaximum = showRadiation_
            ? maximumGain(referenceSamples_) : std::nullopt;
        const auto hasRadiation = maximum.has_value();
        const auto maxGain = maximum.value_or(0.0);
        const auto maximumCurrent = showCurrents_ ? currentMaximum() : std::nullopt;
        if (referenceMaximum)
            drawRadiation(painter, scale, *referenceMaximum, referenceSamples_, true);
        if (hasRadiation) drawRadiation(painter, scale, maxGain, samples_, false);
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
            if (hasRadiation) {
                const auto metrics = analysis::radiationMetrics(samples_, settings_.component);
                status += tr(" · Dataset peak %1").arg(fromCore(analysis::formatGain(
                    {.gainDb = maxGain, .thetaDegrees = metrics.peakThetaDegrees,
                        .phiDegrees = metrics.peakPhiDegrees,
                        .component = settings_.component, .frequencyMHz = settings_.frequencyMHz,
                        .sampleStepDegrees = std::nullopt}).text));
            }
            if (referenceMaximum)
                status += tr(" · Historical overlay %1 shown dashed").arg(referenceLabel_);
            painter.drawText(10, 22, status + tr(" · Shift/middle-drag to pan"));
        }
        if (hasRadiation && showOverlay_) {
            const auto reference = gainReference(maxGain);
            const auto minimum = reference + settings_.floorDb;
            setProperty("colorScaleMinimum", minimum);
            setProperty("colorScaleMaximum", reference);
            drawColorLegend(painter, 42.0,
                settings_.scale == analysis::RadiationScale::Normalized
                    ? tr("Radiation: relative gain · %1")
                          .arg(fromCore(analysis::referenceLevelText(reference)))
                    : tr("Radiation: absolute gain"),
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

    [[nodiscard]] auto maximumGain(
        std::span<const analysis::RadiationSample> samples) const -> std::optional<double>
    {
        auto maximum = -std::numeric_limits<double>::infinity();
        for (const auto& sample : samples) {
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
        const auto maxGain = maximumGain(samples_);
        if (!maxGain) return {};
        const auto reference = gainReference(*maxGain);
        const auto scale = std::min(width(), height()) * 0.38 * zoom_;
        const auto nearest = nearestProjectedSample(position, scale, reference);
        if (!nearest) return {};
        auto text = tr("Nearest NEC sample · %1 (NEC %2) · Gain %3 dBi · Relative %4 dB")
            .arg(fromCore(analysis::formatDirection(
                     nearest->sample->thetaDegrees, nearest->sample->phiDegrees)),
                fromCore(analysis::formatNecDirection(
                    nearest->sample->thetaDegrees, nearest->sample->phiDegrees)),
                formatDecimal(nearest->gain), formatDecimal(nearest->gain - *maxGain));
        if (settings_.scale == analysis::RadiationScale::Normalized)
            text += tr(" · %1").arg(fromCore(analysis::referenceLevelText(*maxGain)));
        if (forwardDirectionCallback_) text += tr(" · Right-click to use as forward direction");
        return text;
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
    void drawRadiation(QPainter& painter, double scale, double maxGain,
        std::span<const analysis::RadiationSample> samples, bool referencePattern)
    {
        struct Vertex {
            double order{};
            QPointF screen;
            double colorRatio{};
        };
        std::map<double, std::vector<Vertex>> phiPaths;
        std::map<double, std::vector<Vertex>> thetaPaths;
        const auto reference = gainReference(maxGain);
        for (const auto& sample : samples) {
            const auto gain = analysis::radiationGainDb(sample, settings_.component);
            if (!std::isfinite(gain) || gain <= -900.0) continue;
            const auto screen = project(samplePoint(sample, gain, reference), scale);
            const auto ratio = gainRatio(gain, reference);
            phiPaths[sample.phiDegrees].push_back({sample.thetaDegrees, screen, ratio});
            thetaPaths[sample.thetaDegrees].push_back({sample.phiDegrees, screen, ratio});
        }
        const auto drawPaths = [&painter, referencePattern](auto& paths,
                                   double width, bool close) {
            for (auto& [key, points] : paths) {
                Q_UNUSED(key);
                std::ranges::sort(points, {}, &Vertex::order);
                for (auto index = std::size_t{1}; index < points.size(); ++index) {
                    const auto ratio = (points[index - 1].colorRatio
                        + points[index].colorRatio) / 2.0;
                    painter.setPen(QPen(referencePattern
                            ? QColor(75, 75, 75, 135) : gainColor(ratio),
                        width, referencePattern ? Qt::DashLine : Qt::SolidLine));
                    painter.drawLine(points[index - 1].screen, points[index].screen);
                }
                if (close && points.size() > 2) {
                    const auto ratio = (points.front().colorRatio
                        + points.back().colorRatio) / 2.0;
                    painter.setPen(QPen(referencePattern
                            ? QColor(75, 75, 75, 135) : gainColor(ratio),
                        width, referencePattern ? Qt::DashLine : Qt::SolidLine));
                    painter.drawLine(points.back().screen, points.front().screen);
                }
            }
        };
        drawPaths(phiPaths, referencePattern ? 1.3 : 2.0, false);
        const auto closeAzimuth = phiPaths.size() >= 3
            && (phiPaths.rbegin()->first - phiPaths.begin()->first) >= 270.0;
        drawPaths(thetaPaths, referencePattern ? 0.8 : 1.0, closeAzimuth);
    }
    void drawAntenna(QPainter& painter, double scale, double extent)
    {
        if (model_.empty()) return;
        painter.setPen(QPen(QColor(210, 155, 35), 1.5));
        painter.setBrush(QColor(245, 190, 45, 75));
        for (const auto& patch : model_.surfacePatches()) {
            QPolygonF polygon;
            for (const auto& point : patch.corners)
                polygon << project(normalized(point, extent), scale);
            if (polygon.size() >= 3) painter.drawPolygon(polygon);
        }
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
        const analysis::WireSegmentIndex index(model_);
        for (const auto& current : currents_) {
            const auto location = index.locate(current);
            if (!location) continue;
            const auto endpoints = model::wireSegmentEndpoints(*location->wire, location->segment);
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
    std::vector<analysis::RadiationSample> referenceSamples_;
    QString referenceLabel_;
    std::vector<analysis::SegmentCurrentResult> currents_;
    analysis::RadiationDisplaySettings settings_;
    QPointF last_;
    QPointF cursor_;
    QPointF pan_;
    HoverTextCallback hoverTextCallback_;
    ForwardDirectionCallback forwardDirectionCallback_;
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
    auto* views = new QTabWidget(this);
    views->setObjectName(QStringLiteral("currentsViews"));
    views->setDocumentMode(true);
    antenna_ = new CurrentAntennaView(views);
    plot_ = new CurrentPlotWidget(views); table_ = new QTableWidget(0, 8, views);
    table_->setHorizontalHeaderLabels({tr("Wire"), tr("Segment"), tr("NEC segment"), tr("Magnitude (A)"),
        tr("NEC phase (°)"), tr("Phase vs. feed (°)"), tr("Real"), tr("Imaginary")});
    table_->horizontalHeaderItem(1)->setToolTip(tr("Segment within its wire, counted from end 1."));
    table_->horizontalHeaderItem(2)->setToolTip(tr("The solver's segment number, counted through all wires. "
        "It tells apart wires that share a tag number."));
    table_->horizontalHeaderItem(4)->setToolTip(tr("Phase as the solver printed it: current flowing from wire end 1 to end 2, "
        "relative to the source voltage."));
    table_->horizontalHeaderItem(5)->setToolTip(tr("Phase relative to the feed current (the first source), with straight wires "
        "taken as flowing toward +X, +Y or +Z so elements entered in opposite directions compare correctly. "
        "The Antenna tab shows this phase."));
    table_->horizontalHeader()->setStretchLastSection(true); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    views->addTab(antenna_, tr("Antenna"));
    views->addTab(plot_, tr("Along Wire"));
    views->addTab(table_, tr("Table"));
    layout->addWidget(summary_); layout->addWidget(views, 1);
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
    for (const auto& value : result_.currents)
        if (nearlyEqual(value.frequencyMHz, selectedFrequency(frequency_))) values.push_back(value);
    summary_->setText(values.empty()
        ? tr("%1 MHz · No segment-current data · %2")
            .arg(formatDecimal(selectedFrequency(frequency_)), runContext_)
        : tr("%1 MHz · %2 current segment(s) · %3")
            .arg(formatDecimal(selectedFrequency(frequency_))).arg(values.size()).arg(runContext_));
    std::vector<analysis::FeedpointResult> feedpoints;
    for (const auto& feed : result_.feedpoints)
        if (nearlyEqual(feed.frequencyMHz, selectedFrequency(frequency_))) feedpoints.push_back(feed);
    auto displayed = analysis::displayedCurrents(values, feedpoints, model_);
    std::map<std::pair<std::size_t, int>, double> displayedPhase;
    for (const auto& segment : displayed.segments)
        displayedPhase[{segment.wireIndex, segment.segment}] = segment.relativePhaseDegrees;
    table_->horizontalHeaderItem(5)->setText(displayed.feedReferenced
        ? tr("Phase vs. feed (°)") : tr("Phase, common direction (°)"));
    antenna_->setCurrents(std::move(displayed), model_);
    plot_->setPaths(connectedCurrentPaths(values, model_)); table_->setRowCount(static_cast<int>(values.size()));
    const analysis::WireSegmentIndex index(model_);
    for (auto row = 0; row < static_cast<int>(values.size()); ++row) {
        const auto location = index.locate(values[row]);
        const auto phase = location
            ? displayedPhase.find({location->wireIndex, location->segment}) : displayedPhase.end();
        const QStringList cells{QString::number(values[row].wireTag),
            QString::number(location ? location->segment : values[row].segment),
            values[row].necSegment > 0 ? QString::number(values[row].necSegment) : tr("—"),
            formatDecimal(values[row].magnitude),
            formatDecimal(values[row].phaseDegrees),
            phase == displayedPhase.end() ? tr("—") : formatDecimal(phase->second),
            formatDecimal(values[row].current.real()),
            formatDecimal(values[row].current.imag())};
        for (auto column = 0; column < cells.size(); ++column) table_->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
}

RadiationPatternView::RadiationPatternView(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QGridLayout;
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation2DFrequency"));
    frequency_->setMinimumContentsLength(20);
    dataset_ = new QComboBox(this); dataset_->setObjectName(QStringLiteral("radiation2DDataset"));
    phi_ = new QComboBox(this); phi_->setObjectName(QStringLiteral("radiation2DCutPlane"));
    cutLabel_ = new QLabel(tr("Elevation cut at Az"), this);
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation2DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation2DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation2DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addWidget(new QLabel(tr("Pattern frequency"), this), 0, 0);
    form->addWidget(frequency_, 0, 1);
    form->addWidget(new QLabel(tr("Pattern dataset"), this), 0, 2);
    form->addWidget(dataset_, 0, 3);
    form->addWidget(new QLabel(tr("Component"), this), 1, 0);
    form->addWidget(component_, 1, 1);
    form->addWidget(new QLabel(tr("Scale"), this), 1, 2);
    form->addWidget(scale_, 1, 3);
    form->addWidget(new QLabel(tr("Dynamic range"), this), 2, 0);
    form->addWidget(floor_, 2, 1);
    form->addWidget(cutLabel_, 2, 2);
    form->addWidget(phi_, 2, 3);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);
    auto* navigation = new QHBoxLayout;
    auto* previous = new QPushButton(tr("◀ Angle"), this);
    previous->setToolTip(tr("Select the previous calculated angle"));
    orientationButton_ = new QPushButton(tr("Show Azimuth Cut"), this);
    orientationButton_->setObjectName(QStringLiteral("radiation2DOrientation"));
    auto* next = new QPushButton(tr("Angle ▶"), this);
    next->setToolTip(tr("Select the next calculated angle"));
    maxGainCutButton_ = new QPushButton(tr("Go to Peak"), this);
    maxGainCutButton_->setToolTip(tr("Move this cut to the strongest sample in the selected RP "
        "dataset: an azimuth cut to that sample's elevation, an elevation cut to its azimuth."));
    maxGainCutButton_->setObjectName(QStringLiteral("radiation2DMaxGainCut"));
    exportImageButton_ = new QPushButton(tr("Image…"), this);
    exportImageButton_->setToolTip(tr("Export pattern image"));
    exportImageButton_->setObjectName(QStringLiteral("radiation2DExportImage"));
    exportDataButton_ = new QPushButton(tr("CSV…"), this);
    exportDataButton_->setToolTip(tr("Export pattern data as CSV"));
    exportDataButton_->setObjectName(QStringLiteral("radiation2DExportData"));
    referenceControl_ = new QCheckBox(tr("Show Historical Overlay"), this);
    referenceControl_->setObjectName(QStringLiteral("radiation2DShowReference"));
    referenceControl_->setChecked(false);
    referenceControl_->setEnabled(false);
    navigation->addWidget(previous); navigation->addWidget(orientationButton_); navigation->addWidget(next);
    navigation->addWidget(maxGainCutButton_);
    navigation->addStretch(); navigation->addWidget(referenceControl_);
    navigation->addWidget(exportImageButton_); navigation->addWidget(exportDataButton_);
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
    connect(frequency_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refreshDatasets(); settingsChanged(); });
    connect(dataset_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refreshCutControls(); });
    connect(component_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refresh(); settingsChanged(); });
    connect(scale_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refresh(); settingsChanged(); });
    connect(floor_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refresh(); settingsChanged(); });
    connect(phi_, &QComboBox::currentIndexChanged, this, [this] {
        datasetSwitchNote_.clear(); refresh(); });
    connect(previous, &QPushButton::clicked, this, [this] { stepAngle(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepAngle(1); });
    connect(orientationButton_, &QPushButton::clicked, this, [this] { toggleOrientation(); });
    connect(maxGainCutButton_, &QPushButton::clicked, this, [this] { showMaxGainCut(); });
    connect(referenceControl_, &QCheckBox::toggled, this, [this] { refresh(); });
    connect(exportImageButton_, &QPushButton::clicked, this, [this] { exportImage(); });
    connect(exportDataButton_, &QPushButton::clicked, this, [this] { exportData(); });
    auto* previousShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    auto* orientationShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    for (auto* shortcut : {previousShortcut, nextShortcut})
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    orientationShortcut->setContext(Qt::WindowShortcut);
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
void RadiationPatternView::setReferenceResults(
    const analysis::AnalysisResult& result, QString label)
{
    referenceResult_ = result;
    referenceLabel_ = std::move(label);
    referenceControl_->setEnabled(true);
    refresh();
}
void RadiationPatternView::clearReferenceResults()
{
    referenceResult_.reset();
    referenceLabel_.clear();
    referenceControl_->setEnabled(false);
    refresh();
}
void RadiationPatternView::setReferenceVisible(bool visible)
{
    referenceControl_->setChecked(visible);
}
void RadiationPatternView::setReferenceControlVisible(bool visible)
{
    referenceControl_->setVisible(visible);
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
void RadiationPatternView::setForwardDirectionCallback(ForwardDirectionCallback callback)
{ plot_->setForwardDirectionCallback(std::move(callback)); }
auto RadiationPatternView::availableCutPlanes(CutOrientation orientation) const -> std::vector<double>
{
    return availableCutPlanes(orientation, selectedPatternIndex(dataset_));
}
auto RadiationPatternView::availableCutPlanes(CutOrientation orientation, int patternIndex) const
    -> std::vector<double>
{
    struct PlaneSamples {
        double plane{};
        std::vector<double> sweepAngles;
    };
    std::vector<PlaneSamples> samplesByPlane;
    for (const auto& value : result_.radiation) {
        if (!nearlyEqual(value.frequencyMHz, selectedFrequency(frequency_))) continue;
        if (value.patternIndex != patternIndex) continue;
        auto plane = orientation == CutOrientation::Vertical ? value.phiDegrees : value.thetaDegrees;
        const auto sweepAngle = orientation == CutOrientation::Vertical
            ? value.thetaDegrees : value.phiDegrees;
        if (orientation == CutOrientation::Vertical) {
            plane = std::fmod(plane + 360.0, 360.0);
            if (plane >= 180.0) plane -= 180.0;
        } else if (nearlyEqual(plane, 0.0) || nearlyEqual(plane, 180.0)) {
            continue;
        }
        auto matchingPlane = std::ranges::find_if(samplesByPlane, [plane](const auto& existing) {
            return nearlyEqual(existing.plane, plane);
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
    // Two samples are only a front/back pair (an elevation cut's 0° and 180° at one
    // elevation), not a pattern.
    for (const auto& samples : samplesByPlane) {
        if (samples.sweepAngles.size() >= 3) planes.push_back(samples.plane);
    }
    std::ranges::sort(planes);
    return planes;
}
auto RadiationPatternView::currentCutPeak() const -> std::optional<analysis::RadiationCutPoint>
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    const auto cut = radiationCutSamples(result_, settings.frequencyMHz,
        selectedPatternIndex(dataset_), orientation_ == CutOrientation::Vertical,
        phi_->currentData().toDouble(), settings.component);
    if (cut.empty()) return std::nullopt;
    return *std::ranges::max_element(cut, {}, &analysis::RadiationCutPoint::gainDb);
}
// Another dataset holding the other kind of cut: prefer the one whose cut crosses the
// current cut at its maximum, then the one with the most samples along that cut.
auto RadiationPatternView::bestDatasetForCut(CutOrientation orientation) const -> std::optional<int>
{
    const auto settings = displaySettings(frequency_, component_, scale_, floor_);
    const auto crossing = currentCutPeak();
    const auto vertical = orientation == CutOrientation::Vertical;
    std::optional<int> best;
    std::pair<bool, std::size_t> bestScore{};
    for (auto index = 0; index < dataset_->count(); ++index) {
        const auto patternIndex = dataset_->itemData(index).toInt();
        if (patternIndex == selectedPatternIndex(dataset_)) continue;
        const auto planes = availableCutPlanes(orientation, patternIndex);
        if (planes.empty()) continue;
        auto samples = std::size_t{};
        if (crossing) {
            auto plane = vertical ? std::fmod(crossing->phiDegrees + 360.0, 360.0)
                                  : crossing->thetaDegrees;
            if (vertical && plane >= 180.0) plane -= 180.0;
            samples = radiationCutSamples(result_, settings.frequencyMHz, patternIndex,
                vertical, plane, settings.component).size();
        }
        const std::pair score{samples >= 3, samples};
        if (!best || score > bestScore) {
            best = patternIndex;
            bestScore = score;
        }
    }
    return best;
}
void RadiationPatternView::selectPlaneThrough(double thetaDegrees, double phiDegrees)
{
    auto plane = orientation_ == CutOrientation::Vertical
        ? std::fmod(phiDegrees + 360.0, 360.0) : thetaDegrees;
    if (orientation_ == CutOrientation::Vertical && plane >= 180.0) plane -= 180.0;
    auto nearestIndex = -1;
    auto nearestDistance = std::numeric_limits<double>::infinity();
    for (auto index = 0; index < phi_->count(); ++index) {
        const auto distance = std::abs(phi_->itemData(index).toDouble() - plane);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestIndex = index;
        }
    }
    if (nearestIndex >= 0) phi_->setCurrentIndex(nearestIndex);
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
        ? tr("Show Azimuth Cut") : tr("Show Elevation Cut"));
    cutLabel_->setText(orientation_ == CutOrientation::Vertical
        ? tr("Elevation cut at Az") : tr("Azimuth cut at El"));
    refreshSelectors();
    // The crossing dataset depends on the selected cut, so decide after the selectors fill.
    const auto oppositeOrientation = orientation_ == CutOrientation::Vertical
        ? CutOrientation::Horizontal : CutOrientation::Vertical;
    const auto otherDataset = availableCutPlanes(oppositeOrientation).empty()
        ? bestDatasetForCut(oppositeOrientation) : std::nullopt;
    const auto oppositeName = oppositeOrientation == CutOrientation::Vertical
        ? tr("elevation cut") : tr("azimuth cut");
    orientationButton_->setEnabled((verticalAvailable && horizontalAvailable)
        || otherDataset.has_value());
    if (verticalAvailable && horizontalAvailable) {
        orientationButton_->setToolTip(tr("Show the %1 crossing this cut at its maximum. "
            "An elevation cut is a side view at one azimuth; an azimuth cut is a top view at "
            "one elevation.").arg(oppositeName));
    } else if (otherDataset) {
        orientationButton_->setToolTip(tr("This dataset has no %1. Switch to %2, whose %1 "
            "crosses this cut at its maximum.")
            .arg(oppositeName, dataset_->itemText(dataset_->findData(*otherDataset))));
    } else if (verticalAvailable || horizontalAvailable) {
        orientationButton_->setToolTip(tr("No RP dataset at this frequency has an %1. Add one "
            "in Analysis › Requests (Add Cuts Through Peak adds a matching pair).")
            .arg(oppositeName));
    } else {
        orientationButton_->setToolTip(tr("The selected RP dataset has insufficient samples for a 2D cut."));
    }
}
void RadiationPatternView::refreshSelectors()
{
    const auto previousValue = phi_->currentData().toDouble();
    phi_->clear();
    for (const auto plane : availableCutPlanes(orientation_)) {
        phi_->addItem(orientation_ == CutOrientation::Vertical
                ? fromCore(analysis::formatAngle(plane))
                : tr("%1 (NEC θ %2)").arg(
                      fromCore(analysis::formatAngle(analysis::patternDirection(plane, 0.0)
                          .elevationDegrees)),
                      fromCore(analysis::formatAngle(plane))),
            plane);
    }
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
    const auto angle = phi_->currentData().toDouble();
    const auto vertical = orientation_ == CutOrientation::Vertical;
    const auto cutSamples = radiationCutSamples(result_, settings.frequencyMHz,
        selectedPatternIndex(dataset_), vertical, angle, settings.component);
    const auto values = plotPoints(cutSamples);
    std::vector<QPointF> referenceValues;
    if (referenceResult_ && referenceControl_->isChecked() && !values.empty()) {
        for (const auto& [patternIndex, stats] : patternDatasets(
                 *referenceResult_, settings.frequencyMHz)) {
            Q_UNUSED(stats);
            auto candidate = plotPoints(radiationCutSamples(*referenceResult_,
                settings.frequencyMHz, patternIndex, vertical, angle, settings.component));
            if (!sameAngularGrid(values, candidate)) continue;
            referenceValues = std::move(candidate);
            break;
        }
    }
    const auto referenceText = !referenceResult_ || !referenceControl_->isChecked() ? QString{}
        : !referenceValues.empty()
            ? tr(" · Historical overlay %1 shown dashed").arg(referenceLabel_)
            : tr(" · Historical overlay %1 unavailable: exact frequency/cut grid differs")
                .arg(referenceLabel_);
    plot_->setCut(vertical, angle);
    const auto cutName = plot_->cutName();
    const auto metrics = analysis::radiationCutMetrics(cutSamples, radiationAnglesCoverCircle(values));
    if (metrics.valid) {
        const auto cutPointAt = [&cutSamples](double plotAngle) {
            return std::ranges::find_if(cutSamples, [plotAngle](const auto& point) {
                return nearlyEqual(point.angleDegrees, plotAngle);
            });
        };
        const auto peakPoint = cutPointAt(metrics.peakAngleDegrees);
        const auto* peakSample = findRadiationSample(result_, settings.frequencyMHz,
            selectedPatternIndex(dataset_), peakPoint->thetaDegrees, peakPoint->phiDegrees);
        const auto peakGain = analysis::formatGain({.gainDb = metrics.peakGainDb,
            .thetaDegrees = peakPoint->thetaDegrees, .phiDegrees = peakPoint->phiDegrees,
            .component = settings.component, .frequencyMHz = settings.frequencyMHz,
            .sampleStepDegrees = peakSample != nullptr
                ? analysis::patternStepAround(result_.radiation, *peakSample) : std::nullopt});
        auto peakText = tr("Peak in this cut %1").arg(fromCore(peakGain.text));
        if (metrics.tiedPeakAnglesDegrees.size() > 1) {
            QStringList tiedAngles;
            for (const auto tiedAngle : metrics.tiedPeakAnglesDegrees) {
                if (nearlyEqual(tiedAngle, metrics.peakAngleDegrees)) continue;
                const auto tied = cutPointAt(tiedAngle);
                tiedAngles.append(fromCore(analysis::formatDirection(
                    tied->thetaDegrees, tied->phiDegrees)));
            }
            if (!tiedAngles.empty()) peakText += tr("; tied at %1").arg(tiedAngles.join(tr("; ")));
        }
        const auto beamwidthText = metrics.interpolatedBeamwidthDegrees
            ? tr("3 dB HPBW %1° interpolated (%2° sampled)")
                .arg(formatDecimal(*metrics.interpolatedBeamwidthDegrees),
                    formatDecimal(*metrics.sampledBeamwidthDegrees))
            : tr("3 dB HPBW unavailable");
        const auto frontToBackText = metrics.frontToBackDb
            ? tr("F/B %1 dB").arg(formatDecimal(*metrics.frontToBackDb))
            : tr("F/B unavailable");
        // A cut can miss the main lobe; point at the strongest sample in all RP data.
        std::vector<analysis::RadiationSample> atFrequency;
        for (const auto& sample : result_.radiation)
            if (nearlyEqual(sample.frequencyMHz, settings.frequencyMHz)) atFrequency.push_back(sample);
        const auto pattern = analysis::radiationMetrics(atFrequency, settings.component);
        QString patternPeakText;
        QString patternPeakToolTip;
        if (pattern.valid && pattern.peakGainDb > metrics.peakGainDb + 1.0e-6) {
            const auto* sample = &*std::ranges::find_if(atFrequency, [&pattern](const auto& value) {
                return value.thetaDegrees == pattern.peakThetaDegrees
                    && value.phiDegrees == pattern.peakPhiDegrees;
            });
            const auto patternGain = analysis::formatGain({.gainDb = pattern.peakGainDb,
                .thetaDegrees = pattern.peakThetaDegrees, .phiDegrees = pattern.peakPhiDegrees,
                .component = settings.component, .frequencyMHz = settings.frequencyMHz,
                .sampleStepDegrees = analysis::patternStepAround(atFrequency, *sample)});
            patternPeakText = tr(" · Sampled peak (all RP data) %1, outside this cut")
                .arg(fromCore(patternGain.text));
            patternPeakToolTip = tr("\nSampled peak: strongest sample in all RP data at this "
                "frequency, not in this cut · %1").arg(fromCore(patternGain.toolTip));
        }
        const auto switchNote = datasetSwitchNote_.isEmpty()
            ? QString{} : datasetSwitchNote_ + QStringLiteral(" · ");
        summary_->setText(switchNote + tr("%1 MHz · %2 · %3 · %4 · %5 · %6%7 · Left/Right changes angle · Space changes cut")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, cutName, peakText,
                beamwidthText, frontToBackText, patternPeakText) + referenceText);
        summary_->setToolTip(tr("Peak in this cut: strongest sample in this cut (%1) · %2\n"
            "HPBW: 3 dB beamwidth within this cut.\n%3 Forward is the cut maximum.%4")
            .arg(cutName, fromCore(peakGain.toolTip),
                fromCore(analysis::frontToBackDefinition()), patternPeakToolTip));
    } else {
        summary_->setText(tr("%1 MHz · %2 · No %3 samples for this cut · %4")
            .arg(formatDecimal(settings.frequencyMHz), cutName,
                componentName(settings.component), runContext_)
            + referenceText);
        summary_->setToolTip({});
    }
    exportImageButton_->setEnabled(metrics.valid);
    exportDataButton_->setEnabled(metrics.valid);
    const auto patternIndex = selectedPatternIndex(dataset_);
    maxGainCutButton_->setEnabled(std::ranges::any_of(result_.radiation, [settings, patternIndex](const auto& sample) {
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return nearlyEqual(sample.frequencyMHz, settings.frequencyMHz)
            && sample.patternIndex == patternIndex
            && std::isfinite(gain) && gain > -900.0;
    }));
    plot_->setDisplaySettings(settings);
    plot_->setReferenceSamples(std::move(referenceValues), referenceLabel_);
    plot_->setSamples(cutSamples);
}
void RadiationPatternView::toggleOrientation()
{
    // Land on the cut that crosses the current one at its maximum.
    const auto crossing = currentCutPeak();
    const auto next = orientation_ == CutOrientation::Vertical
        ? CutOrientation::Horizontal : CutOrientation::Vertical;
    auto switchedDataset = false;
    if (availableCutPlanes(next).empty()) {
        const auto patternIndex = bestDatasetForCut(next);
        if (!patternIndex) return;
        orientation_ = next;
        dataset_->setCurrentIndex(dataset_->findData(*patternIndex));
        switchedDataset = true;
    } else {
        orientation_ = next;
        refreshCutControls();
    }
    if (crossing) selectPlaneThrough(crossing->thetaDegrees, crossing->phiDegrees);
    // Set after the programmatic changes above, which would otherwise clear it.
    if (switchedDataset) {
        datasetSwitchNote_ = tr("Switched to %1, crossing the previous cut's peak")
            .arg(dataset_->currentText());
        refresh();
    }
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
        if (!nearlyEqual(sample.frequencyMHz, settings.frequencyMHz)
            || sample.patternIndex != patternIndex)
            return -std::numeric_limits<double>::infinity();
        const auto gain = analysis::radiationGainDb(sample, settings.component);
        return std::isfinite(gain) && gain > -900.0
            ? gain : -std::numeric_limits<double>::infinity();
    });
    if (peak == result_.radiation.end()
        || !nearlyEqual(peak->frequencyMHz, settings.frequencyMHz)) return;

    // Stay in the current kind of cut: an azimuth cut moves to the peak's elevation and an
    // elevation cut to the peak's azimuth. Switch only when this dataset lacks that kind.
    if (availableCutPlanes(orientation_).empty()) {
        const auto other = orientation_ == CutOrientation::Vertical
            ? CutOrientation::Horizontal : CutOrientation::Vertical;
        if (availableCutPlanes(other).empty()) return;
        orientation_ = other;
    }
    refreshCutControls();
    selectPlaneThrough(peak->thetaDegrees, peak->phiDegrees);
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
        if (!nearlyEqual(sample.frequencyMHz, settings.frequencyMHz)) continue;
        if (sample.patternIndex != selectedPatternIndex(dataset_)) continue;
        auto included = false;
        auto angle = 0.0;
        if (orientation_ == CutOrientation::Horizontal
            && nearlyEqual(sample.thetaDegrees, selectedPlane)) {
            included = true;
            angle = sample.phiDegrees;
        }
        if (orientation_ == CutOrientation::Vertical) {
            const auto phi = std::fmod(sample.phiDegrees + 360.0, 360.0);
            const auto opposite = std::fmod(selectedPlane + 180.0, 360.0);
            if (nearlyEqual(phi, selectedPlane)) {
                included = true;
                angle = sample.thetaDegrees;
            } else if (nearlyEqual(phi, opposite)) {
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
    auto* form = new QGridLayout;
    frequency_ = new QComboBox(this); frequency_->setObjectName(QStringLiteral("radiation3DFrequency"));
    frequency_->setMinimumContentsLength(20);
    dataset_ = new QComboBox(this); dataset_->setObjectName(QStringLiteral("radiation3DDataset"));
    component_ = new QComboBox(this); component_->setObjectName(QStringLiteral("radiation3DComponent"));
    scale_ = new QComboBox(this); scale_->setObjectName(QStringLiteral("radiation3DScale"));
    floor_ = new QComboBox(this); floor_->setObjectName(QStringLiteral("radiation3DFloor"));
    populateRadiationControls(component_, scale_, floor_);
    form->addWidget(new QLabel(tr("Pattern frequency"), this), 0, 0);
    form->addWidget(frequency_, 0, 1);
    form->addWidget(new QLabel(tr("Pattern dataset"), this), 0, 2);
    form->addWidget(dataset_, 0, 3);
    form->addWidget(new QLabel(tr("Component"), this), 1, 0);
    form->addWidget(component_, 1, 1);
    form->addWidget(new QLabel(tr("Scale"), this), 1, 2);
    form->addWidget(scale_, 1, 3);
    form->addWidget(new QLabel(tr("Dynamic range"), this), 2, 0);
    form->addWidget(floor_, 2, 1);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);
    auto* layers = new QHBoxLayout;
    antennaControl_ = new QCheckBox(tr("Antenna"), this); antennaControl_->setChecked(true);
    currentControl_ = new QCheckBox(tr("Current Overlay"), this); currentControl_->setChecked(true);
    radiationControl_ = new QCheckBox(tr("Radiation Surface"), this); radiationControl_->setChecked(true);
    referenceControl_ = new QCheckBox(tr("Show Historical Overlay"), this);
    referenceControl_->setObjectName(QStringLiteral("radiation3DShowReference"));
    referenceControl_->setChecked(false);
    referenceControl_->setEnabled(false);
    auto* resetView = new QPushButton(tr("Reset View"), this);
    exportImageButton_ = new QPushButton(tr("Export Image…"), this);
    exportImageButton_->setObjectName(QStringLiteral("radiation3DExportImage"));
    exportDataButton_ = new QPushButton(tr("Export CSV…"), this);
    exportDataButton_->setObjectName(QStringLiteral("radiation3DExportData"));
    layers->addWidget(antennaControl_); layers->addWidget(currentControl_);
    layers->addWidget(radiationControl_); layers->addWidget(referenceControl_);
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
    connect(referenceControl_, &QCheckBox::toggled, this, [this] { refresh(); });
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
void Radiation3DView::setReferenceResults(
    const analysis::AnalysisResult& result, QString label)
{
    referenceResult_ = result;
    referenceLabel_ = std::move(label);
    referenceControl_->setEnabled(true);
    refresh();
}
void Radiation3DView::clearReferenceResults()
{
    referenceResult_.reset();
    referenceLabel_.clear();
    referenceControl_->setEnabled(false);
    refresh();
}
void Radiation3DView::setReferenceVisible(bool visible)
{
    referenceControl_->setChecked(visible);
}
void Radiation3DView::setReferenceControlVisible(bool visible)
{
    referenceControl_->setVisible(visible);
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
    std::vector<analysis::RadiationSample> referenceRadiation;
    std::vector<analysis::SegmentCurrentResult> currents;
    for (const auto& value : result_.radiation)
        if (nearlyEqual(value.frequencyMHz, selectedFrequency(frequency_))
            && value.patternIndex == selectedPatternIndex(dataset_)) radiation.push_back(value);
    if (referenceResult_ && referenceControl_->isChecked()) {
        const auto activeDatasets = patternDatasets(result_, settings.frequencyMHz);
        const auto activeDataset = activeDatasets.find(selectedPatternIndex(dataset_));
        if (activeDataset != activeDatasets.end()) {
            for (const auto& [referenceIndex, stats] : patternDatasets(
                     *referenceResult_, settings.frequencyMHz)) {
                if (!samePatternGrid(activeDataset->second, stats)) continue;
                for (const auto& value : referenceResult_->radiation) {
                    if (nearlyEqual(value.frequencyMHz, settings.frequencyMHz)
                        && value.patternIndex == referenceIndex)
                        referenceRadiation.push_back(value);
                }
                break;
            }
        }
    }
    for (const auto& value : result_.currents)
        if (nearlyEqual(value.frequencyMHz, selectedFrequency(frequency_))) currents.push_back(value);
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
        const auto peak = std::ranges::find_if(radiation, [&metrics](const auto& sample) {
            return sample.thetaDegrees == metrics.peakThetaDegrees
                && sample.phiDegrees == metrics.peakPhiDegrees;
        });
        const auto peakGain = analysis::formatGain({.gainDb = metrics.peakGainDb,
            .thetaDegrees = metrics.peakThetaDegrees, .phiDegrees = metrics.peakPhiDegrees,
            .component = settings.component, .frequencyMHz = settings.frequencyMHz,
            .sampleStepDegrees = analysis::patternStepAround(radiation, *peak)});
        auto text = tr("%1 MHz · %2 · Dataset peak %3 · %4 · %5 phi plane(s)")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, fromCore(peakGain.text),
                frontToBackText)
            .arg(planes.size());
        summary_->setToolTip(tr("Dataset peak: strongest sample in this RP dataset · %1\n"
            "%2 Forward is the dataset peak.")
            .arg(fromCore(peakGain.toolTip), fromCore(analysis::frontToBackDefinition())));
        if (metrics.tiedPeaks.size() > 1)
            text += tr(" · %1 tied peak directions").arg(metrics.tiedPeaks.size());
        if (planes.size() < 3)
            text += tr(" · Partial coverage; request Full 3D Pattern for a complete surface.");
        if (referenceResult_ && referenceControl_->isChecked())
            text += referenceRadiation.empty()
                ? tr(" · Historical overlay %1 unavailable: exact frequency/grid differs")
                      .arg(referenceLabel_)
                : tr(" · Historical overlay %1 shown dashed").arg(referenceLabel_);
        summary_->setText(text);
    } else {
        summary_->setToolTip({});
        summary_->setText(tr("%1 MHz · %2 · No %3 radiation samples · %4 current segment(s)")
            .arg(formatDecimal(settings.frequencyMHz), runContext_, componentName(settings.component))
            .arg(currents.size()));
    }
    exportImageButton_->setEnabled(!radiation.empty() || !currents.empty() || !model_.empty());
    exportDataButton_->setEnabled(!radiation.empty());
    surface_->setDisplaySettings(settings);
    surface_->setReferenceSamples(std::move(referenceRadiation), referenceLabel_);
    surface_->setSamples(std::move(radiation)); surface_->setCurrents(std::move(currents));
}
void Radiation3DView::setForwardDirectionCallback(ForwardDirectionCallback callback)
{
    surface_->setForwardDirectionCallback(std::move(callback));
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
        if (!nearlyEqual(sample.frequencyMHz, settings.frequencyMHz)) continue;
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
    forwardElevation_ = new QDoubleSpinBox(this);
    forwardElevation_->setObjectName(QStringLiteral("radiationPerformanceElevation"));
    forwardElevation_->setRange(-90.0, 90.0);
    forwardElevation_->setDecimals(DisplayDecimalPlaces);
    forwardElevation_->setSuffix(QStringLiteral("°"));
    forwardAzimuth_ = new QDoubleSpinBox(this);
    forwardAzimuth_->setObjectName(QStringLiteral("radiationPerformanceAzimuth"));
    forwardAzimuth_->setRange(-360.0, 720.0);
    forwardAzimuth_->setDecimals(DisplayDecimalPlaces);
    forwardAzimuth_->setSuffix(QStringLiteral("°"));
    usePatternPeak_ = new QPushButton(tr("Use Sampled Peak"), this);
    usePatternPeak_->setObjectName(QStringLiteral("radiationPerformanceUsePatternPeak"));
    const auto directionTip = tr(
        "Physical forward direction used for gain, F/B, and F/R. Elevation is measured "
        "above the horizon; NEC theta equals 90 degrees minus elevation. Exact RP samples "
        "must exist at the requested direction; values are not interpolated.");
    forwardElevation_->setToolTip(directionTip);
    forwardAzimuth_->setToolTip(directionTip);
    usePatternPeak_->setToolTip(tr(
        "Set the direction to the sampled peak: the strongest gain sample in all RP data at "
        "the selected frequency."));
    controls->addWidget(new QLabel(tr("Component"), this));
    controls->addWidget(component_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Forward elevation"), this));
    controls->addWidget(forwardElevation_);
    controls->addWidget(new QLabel(tr("Azimuth"), this));
    controls->addWidget(forwardAzimuth_);
    controls->addWidget(usePatternPeak_);
    controls->addStretch();
    referenceControl_ = new QCheckBox(tr("Show Historical Overlay"), this);
    referenceControl_->setObjectName(QStringLiteral("radiationPerformanceShowReference"));
    referenceControl_->setChecked(false);
    referenceControl_->setEnabled(false);
    controls->addWidget(referenceControl_);
    readout_ = new QLabel(this);
    readout_->setObjectName(QStringLiteral("radiationPerformanceReadout"));
    readout_->setWordWrap(true);
    auto readoutFont = readout_->font();
    readoutFont.setBold(true);
    readoutFont.setPointSizeF(readoutFont.pointSizeF() * 1.15);
    readout_->setFont(readoutFont);
    summary_ = new QLabel(tr(
        "Load radiation results to inspect directional performance across frequency."), this);
    summary_->setObjectName(QStringLiteral("radiationPerformanceSummary"));
    summary_->setWordWrap(true);
    plot_ = new DirectionalMetricsView(this);
    plot_->setObjectName(QStringLiteral("radiationPerformancePlot"));
    plot_->setToolTip(tr("Gain is toward the forward direction above.\n%1\n%2")
        .arg(fromCore(analysis::frontToBackDefinition()),
            fromCore(analysis::frontToRearDefinition(
                analysis::DirectionalEvaluationMode::SpecifiedDirection))));
    layout->addLayout(controls);
    layout->addWidget(readout_);
    layout->addWidget(summary_);
    layout->addWidget(plot_, 1);
    connect(component_, &QComboBox::currentIndexChanged, this, [this] {
        if (directionCustomized_) refresh();
        else seedDirectionFromPatternPeak();
        if (componentChangedCallback_) {
            componentChangedCallback_(static_cast<analysis::RadiationComponent>(
                component_->currentData().toInt()));
        }
    });
    const auto directionChanged = [this] {
        if (!updatingDirection_) directionCustomized_ = true;
        refresh();
    };
    connect(forwardElevation_, &QDoubleSpinBox::valueChanged, this, directionChanged);
    connect(forwardAzimuth_, &QDoubleSpinBox::valueChanged, this, directionChanged);
    connect(usePatternPeak_, &QPushButton::clicked, this, [this] {
        directionCustomized_ = false;
        seedDirectionFromPatternPeak();
    });
    connect(referenceControl_, &QCheckBox::toggled, this, [this] { refresh(); });
}

void RadiationPerformanceView::setResults(
    const analysis::AnalysisResult& result, const QString& runDirectory)
{
    result_ = result;
    runContext_ = runDirectory;
    directionCustomized_ = false;
    seedDirectionFromPatternPeak();
}

void RadiationPerformanceView::setReferenceResults(
    const analysis::AnalysisResult& result, QString label)
{
    referenceResult_ = result;
    referenceLabel_ = std::move(label);
    referenceControl_->setEnabled(true);
    refresh();
}

void RadiationPerformanceView::clearReferenceResults()
{
    referenceResult_.reset();
    referenceLabel_.clear();
    referenceControl_->setEnabled(false);
    refresh();
}
void RadiationPerformanceView::setReferenceVisible(bool visible)
{
    referenceControl_->setChecked(visible);
}

void RadiationPerformanceView::setReferenceControlVisible(bool visible)
{
    referenceControl_->setVisible(visible);
}

void RadiationPerformanceView::setSelectedFrequency(double frequencyMHz)
{
    selectedFrequencyMHz_ = frequencyMHz;
    plot_->setSelectedFrequency(frequencyMHz);
    if (directionCustomized_) refresh();
    else seedDirectionFromPatternPeak();
}

void RadiationPerformanceView::setComponent(analysis::RadiationComponent component)
{
    const QSignalBlocker blocker(component_);
    const auto index = component_->findData(static_cast<int>(component));
    if (index >= 0) component_->setCurrentIndex(index);
    if (directionCustomized_) refresh();
    else seedDirectionFromPatternPeak();
}

void RadiationPerformanceView::setComponentChangedCallback(
    ComponentChangedCallback callback)
{
    componentChangedCallback_ = std::move(callback);
}

void RadiationPerformanceView::setForwardDirection(double thetaDegrees, double phiDegrees)
{
    const auto direction = analysis::patternDirection(thetaDegrees, phiDegrees);
    updatingDirection_ = true;
    forwardElevation_->setValue(direction.elevationDegrees);
    forwardAzimuth_->setValue(direction.azimuthDegrees);
    updatingDirection_ = false;
    directionCustomized_ = true;
    refresh();
}

void RadiationPerformanceView::seedDirectionFromPatternPeak()
{
    const auto component = static_cast<analysis::RadiationComponent>(
        component_->currentData().toInt());
    const auto targetFrequency = selectedFrequencyMHz_.value_or(
        result_.radiation.empty() ? 0.0 : result_.radiation.front().frequencyMHz);
    const auto peak = analysis::radiationPeakAtNearestFrequency(
        result_.radiation, component, targetFrequency);
    usePatternPeak_->setEnabled(peak.has_value());
    if (!peak) {
        refresh();
        return;
    }

    updatingDirection_ = true;
    forwardElevation_->setValue(90.0 - peak->metrics.peakThetaDegrees);
    forwardAzimuth_->setValue(peak->metrics.peakPhiDegrees);
    updatingDirection_ = false;
    refresh();
}

void RadiationPerformanceView::refresh()
{
    const auto component = static_cast<analysis::RadiationComponent>(
        component_->currentData().toInt());
    const auto thetaDegrees = 90.0 - forwardElevation_->value();
    const auto phiDegrees = forwardAzimuth_->value();
    const auto metrics = analysis::radiationFrequencyMetrics(result_.radiation, component,
        thetaDegrees, phiDegrees);
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
    if (referenceResult_ && referenceControl_->isChecked()) {
        const auto referenceMetrics = analysis::radiationFrequencyMetrics(
            referenceResult_->radiation, component,
            thetaDegrees, phiDegrees);
        std::vector<DirectionalPlotPoint> referencePoints;
        referencePoints.reserve(referenceMetrics.size());
        for (const auto& metric : referenceMetrics) {
            referencePoints.push_back({metric.frequencyMHz, metric.forwardGainDb,
                metric.frontToBackDb, metric.frontToRearDb});
        }
        plot_->setReferenceMetrics(referencePoints, referenceLabel_);
    } else {
        plot_->clearReferenceMetrics();
    }
    if (selectedFrequencyMHz_) plot_->setSelectedFrequency(*selectedFrequencyMHz_);
    if (metrics.empty()) {
        readout_->clear();
        readout_->setToolTip({});
        summary_->setText(tr("No radiation samples are available · %1").arg(runContext_));
        return;
    }
    const auto selectedMetric = std::ranges::min_element(metrics,
        [targetFrequency = selectedFrequencyMHz_.value_or(metrics.front().frequencyMHz)](
            const auto& left, const auto& right) {
            return std::abs(left.frequencyMHz - targetFrequency)
                < std::abs(right.frequencyMHz - targetFrequency);
        });
    const auto direction = tr("%1 (NEC %2)")
        .arg(fromCore(analysis::formatDirection(thetaDegrees, phiDegrees)),
            fromCore(analysis::formatNecDirection(thetaDegrees, phiDegrees)));
    const auto toward = fromCore(analysis::formatDirection(thetaDegrees, phiDegrees));
    const auto componentText = component == analysis::RadiationComponent::Total
        ? QString{} : tr(" (%1)").arg(componentName(component).toLower());
    QStringList readout;
    readout.append(selectedMetric->forwardGainDb
        ? tr("Gain toward %1%2: %3 dBi").arg(toward, componentText,
              formatDecimal(*selectedMetric->forwardGainDb))
        : tr("Gain toward %1%2: unavailable").arg(toward, componentText));
    if (selectedMetric->frontToBackDb)
        readout.append(tr("F/B %1 dB").arg(formatDecimal(*selectedMetric->frontToBackDb)));
    if (selectedMetric->frontToRearDb)
        readout.append(tr("F/R %1 dB").arg(formatDecimal(*selectedMetric->frontToRearDb)));
    readout_->setText(tr("%1 · %2 MHz").arg(readout.join(QStringLiteral(" · ")),
        formatDecimal(selectedMetric->frequencyMHz)));
    readout_->setToolTip(tr("Forward direction %1 · %2 gain\n%3\n%4\n"
        "Set the direction above, use Sampled Peak, or right-click a 2D or 3D pattern.")
        .arg(direction, componentName(component),
            fromCore(analysis::frontToBackDefinition()),
            fromCore(analysis::frontToRearDefinition(
                analysis::DirectionalEvaluationMode::SpecifiedDirection))));
    if (selectedMetric->forwardAvailability == analysis::RadiationMetricAvailability::PatternNull) {
        summary_->setText(tr(
            "%1 is a pattern null at %2 MHz. Choose another sampled direction or use "
            "Sampled Peak. · %3")
            .arg(direction, formatDecimal(selectedMetric->frequencyMHz), runContext_));
        return;
    }
    if (selectedMetric->forwardAvailability == analysis::RadiationMetricAvailability::Missing) {
        summary_->setText(tr(
            "%1 has no matching RP sample at %2 MHz. Exact samples are not interpolated; "
            "choose another direction or use Sampled Peak. · %3")
            .arg(direction, formatDecimal(selectedMetric->frequencyMHz), runContext_));
        return;
    }
    summary_->setText(tr(
        "%1 · %2 · Available frequencies: Gain %3/%6, F/B %4/%6, F/R %5/%6 · "
        "Missing exact directions remain unavailable · %7")
        .arg(direction, componentName(component))
        .arg(gainCount).arg(frontToBackCount).arg(frontToRearCount).arg(metrics.size())
        .arg(runContext_));
}

}
