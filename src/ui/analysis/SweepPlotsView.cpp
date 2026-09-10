#include "ui/analysis/SweepPlotsView.h"

#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>
#include <QSplitter>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace necwb::ui {
namespace {

enum class AxisSide {
    Left,
    Right,
};

enum class AxisScale {
    Linear,
    Log10,
};

enum class TickStyle {
    Linear,
    Swr,
    Resistance,
};

enum class SwrScaleMode {
    Logarithmic,
    Linear,
    LimitThree,
    LimitFive,
};

enum class PlotKind {
    Impedance,
    Swr,
    Linear,
};

struct PlotSeries {
    QString name;
    QString unit;
    QColor color;
    AxisSide axis{AxisSide::Left};
    std::vector<QPointF> points;
};

struct AxisTick {
    double value{};
    double position{};
};

struct PlotAxis {
    QString label;
    QColor color;
    AxisScale scale{AxisScale::Linear};
    TickStyle tickStyle{TickStyle::Linear};
    double minimum{};
    double maximum{1.0};
    std::vector<AxisTick> ticks;
    bool valid{};
};

auto formatAxisValue(double value) -> QString
{
    if (std::abs(value) >= 10000.0 || (std::abs(value) > 0.0 && std::abs(value) < 0.01))
        return QString::number(value, 'g', 3);
    if (std::abs(value - std::round(value)) < 1.0e-9)
        return QString::number(static_cast<qlonglong>(std::llround(value)));
    return QString::number(value, 'f', std::abs(value) < 2.0 ? 1 : 0);
}

auto transformed(double value, AxisScale scale) -> double
{
    if (scale == AxisScale::Linear) return value;
    return value > 0.0 ? std::log10(value) : std::numeric_limits<double>::quiet_NaN();
}

auto axisPosition(double value, const PlotAxis& axis) -> double
{
    const auto mapped = transformed(value, axis.scale);
    const auto minimum = transformed(axis.minimum, axis.scale);
    const auto maximum = transformed(axis.maximum, axis.scale);
    if (!std::isfinite(mapped) || !std::isfinite(minimum) || maximum <= minimum)
        return std::numeric_limits<double>::quiet_NaN();
    return (mapped - minimum) / (maximum - minimum);
}

auto niceLinearStep(double range) -> double
{
    const auto rawStep = std::max(1.0, range / 6.0);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const auto normalized = rawStep / magnitude;
    const auto multiplier = normalized <= 1.0 ? 1.0
        : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
    return multiplier * magnitude;
}

auto makeLinearAxis(QString label, QColor color, const std::vector<double>& values,
    bool includeZero, std::optional<std::pair<double, double>> fixedRange = {}) -> PlotAxis
{
    PlotAxis axis;
    axis.label = std::move(label);
    axis.color = std::move(color);
    axis.scale = AxisScale::Linear;
    axis.tickStyle = TickStyle::Linear;
    if (fixedRange) {
        axis.minimum = fixedRange->first;
        axis.maximum = fixedRange->second;
    } else {
        auto minimum = std::numeric_limits<double>::infinity();
        auto maximum = -std::numeric_limits<double>::infinity();
        for (const auto value : values) {
            if (!std::isfinite(value)) continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        if (!std::isfinite(minimum)) return axis;
        const auto dataMinimum = minimum;
        const auto dataMaximum = maximum;
        if (includeZero) {
            minimum = std::min(0.0, minimum);
            maximum = std::max(0.0, maximum);
        }
        if (minimum == maximum) {
            const auto padding = std::max(1.0, std::abs(minimum) * 0.1);
            minimum -= padding;
            maximum += padding;
        } else {
            const auto padding = (maximum - minimum) * 0.06;
            minimum -= padding;
            maximum += padding;
        }
        const auto step = niceLinearStep(maximum - minimum);
        axis.minimum = std::floor(minimum / step) * step;
        axis.maximum = std::ceil(maximum / step) * step;
        if (includeZero && dataMinimum >= 0.0) axis.minimum = 0.0;
        if (includeZero && dataMaximum <= 0.0) axis.maximum = 0.0;
    }
    if (axis.maximum <= axis.minimum) return axis;
    const auto step = niceLinearStep(axis.maximum - axis.minimum);
    for (auto value = axis.minimum; value <= axis.maximum + step * 0.25; value += step)
        axis.ticks.push_back({value, (value - axis.minimum) / (axis.maximum - axis.minimum)});
    axis.valid = true;
    return axis;
}

auto engineeringCeiling(double value, std::span<const double> multipliers) -> double
{
    if (!std::isfinite(value) || value <= 0.0) return 1.0;
    const auto decade = std::pow(10.0, std::floor(std::log10(value)));
    for (const auto multiplier : multipliers)
        if (multiplier * decade >= value) return multiplier * decade;
    return 10.0 * decade;
}

auto makeLogAxis(QString label, QColor color, const std::vector<double>& values,
    TickStyle tickStyle, std::optional<double> fixedMinimum = {}) -> PlotAxis
{
    PlotAxis axis;
    axis.label = std::move(label);
    axis.color = std::move(color);
    axis.scale = AxisScale::Log10;
    axis.tickStyle = tickStyle;
    auto minimum = std::numeric_limits<double>::infinity();
    auto maximum = -std::numeric_limits<double>::infinity();
    for (const auto value : values) {
        if (!std::isfinite(value) || value <= 0.0) continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (!std::isfinite(minimum)) return axis;

    if (fixedMinimum) {
        axis.minimum = *fixedMinimum;
        constexpr std::array swrMultipliers{1.0, 1.2, 1.5, 2.0, 3.0, 5.0, 10.0};
        axis.maximum = engineeringCeiling(std::max(maximum, axis.minimum * 1.01), swrMultipliers);
    } else {
        axis.minimum = std::pow(10.0, std::floor(std::log10(minimum)));
        constexpr std::array resistanceMultipliers{1.0, 2.0, 4.0, 10.0};
        axis.maximum = engineeringCeiling(maximum, resistanceMultipliers);
    }
    if (axis.maximum <= axis.minimum) axis.maximum = axis.minimum * 10.0;

    const auto addTicks = [&axis](std::span<const double> multipliers) {
        const auto firstDecade = static_cast<int>(std::floor(std::log10(axis.minimum))) - 1;
        const auto lastDecade = static_cast<int>(std::ceil(std::log10(axis.maximum))) + 1;
        for (auto exponent = firstDecade; exponent <= lastDecade; ++exponent) {
            const auto decade = std::pow(10.0, exponent);
            for (const auto multiplier : multipliers) {
                const auto value = multiplier * decade;
                if (value < axis.minimum * (1.0 - 1.0e-9)
                    || value > axis.maximum * (1.0 + 1.0e-9)) continue;
                axis.ticks.push_back({value, axisPosition(value, axis)});
            }
        }
    };
    if (tickStyle == TickStyle::Swr) {
        constexpr std::array multipliers{1.0, 1.2, 1.5, 2.0, 3.0, 5.0};
        addTicks(multipliers);
    } else {
        constexpr std::array multipliers{1.0, 2.0, 4.0};
        addTicks(multipliers);
    }
    axis.valid = !axis.ticks.empty();
    return axis;
}

auto collectValues(const std::vector<PlotSeries>& series, AxisSide side) -> std::vector<double>
{
    std::vector<double> values;
    for (const auto& item : series) {
        if (item.axis != side) continue;
        for (const auto& point : item.points) values.push_back(point.y());
    }
    return values;
}

}

class SweepPlotWidget final : public QWidget {
public:
    explicit SweepPlotWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(240);
        setMouseTracking(true);
    }

    void setImpedancePlot(std::vector<PlotSeries> series)
    {
        title_ = tr("Feedpoint Resistance and Reactance");
        xAxisLabel_ = tr("Frequency (MHz)");
        series_ = std::move(series);
        plotKind_ = PlotKind::Impedance;
        rebuildAxes();
        clearHover();
        update();
    }

    void setSwrPlot(QString label, std::vector<PlotSeries> series)
    {
        title_ = tr("Standing-Wave Ratio");
        xAxisLabel_ = tr("Frequency (MHz)");
        leftAxisLabel_ = std::move(label);
        series_ = std::move(series);
        plotKind_ = PlotKind::Swr;
        rebuildAxes();
        clearHover();
        update();
    }

    void setSelectedFrequency(double frequencyMHz)
    {
        selectedFrequencyMHz_ = frequencyMHz;
        selectedCaption_ = tr("Selected %1 MHz").arg(formatDecimal(frequencyMHz));
        update();
    }

    void setLinearPlot(QString title, QString xAxisLabel, QString yAxisLabel,
        std::vector<PlotSeries> series, std::optional<double> selectedX = {})
    {
        title_ = std::move(title);
        xAxisLabel_ = std::move(xAxisLabel);
        leftAxisLabel_ = std::move(yAxisLabel);
        series_ = std::move(series);
        plotKind_ = PlotKind::Linear;
        selectedFrequencyMHz_ = selectedX.value_or(
            std::numeric_limits<double>::quiet_NaN());
        selectedCaption_ = selectedX
            ? tr("Best: %1").arg(formatDecimal(*selectedX)) : QString{};
        rebuildAxes();
        clearHover();
        setProperty("pointCount", series_.empty()
            ? 0 : static_cast<int>(series_.front().points.size()));
        setProperty("xValuesAscending", series_.empty()
            || std::ranges::is_sorted(series_.front().points, {}, &QPointF::x));
        setProperty("bestX", selectedFrequencyMHz_);
        update();
    }

    void setXActivatedCallback(std::function<void(double)> callback)
    {
        xActivatedCallback_ = std::move(callback);
    }

    void setSwrScaleMode(SwrScaleMode mode)
    {
        swrScaleMode_ = mode;
        setProperty("scaleMode", static_cast<int>(mode));
        rebuildAxes();
        clearHover();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        painter.setPen(palette().color(QPalette::Text));
        auto titleFont = painter.font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(QRectF(14, 8, width() - 28, 24), Qt::AlignCenter, title_);
        titleFont.setBold(false);
        painter.setFont(titleFont);

        const auto dualAxes = plotKind_ == PlotKind::Impedance;
        plotRect_ = QRectF(96, 68, std::max(1, width() - (dualAxes ? 194 : 118)),
            std::max(1, height() - 116));
        setPlotProperties();
        if (!boundsValid_) {
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawText(plotRect_, Qt::AlignCenter,
                tr("Run a frequency analysis to populate this plot."));
            return;
        }

        drawLegend(painter);
        drawAxes(painter);
        drawSeries(painter);
        drawSelectedFrequency(painter);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        auto nearestDistance = 12.0;
        auto nearestSeries = -1;
        auto nearestPoint = -1;
        if (boundsValid_) {
            for (auto seriesIndex = 0; seriesIndex < static_cast<int>(series_.size()); ++seriesIndex) {
                const auto& axis = series_[seriesIndex].axis == AxisSide::Left ? leftAxis_ : rightAxis_;
                for (auto pointIndex = 0;
                     pointIndex < static_cast<int>(series_[seriesIndex].points.size()); ++pointIndex) {
                    const auto screenPoint = mapPoint(series_[seriesIndex].points[pointIndex], axis);
                    if (!std::isfinite(screenPoint.x()) || !std::isfinite(screenPoint.y())) continue;
                    const auto distance = std::hypot(screenPoint.x() - event->position().x(),
                        screenPoint.y() - event->position().y());
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearestSeries = seriesIndex;
                        nearestPoint = pointIndex;
                    }
                }
            }
        }
        if (nearestSeries != hoveredSeries_ || nearestPoint != hoveredPoint_) {
            hoveredSeries_ = nearestSeries;
            hoveredPoint_ = nearestPoint;
            update();
        }
        if (hoveredSeries_ < 0) {
            QToolTip::hideText();
            return;
        }
        const auto frequency = series_[hoveredSeries_].points[hoveredPoint_].x();
        QStringList rows{tr("%1: %2").arg(xAxisLabel_, formatDecimal(frequency))};
        for (const auto& series : series_) {
            const auto found = std::ranges::find_if(series.points, [frequency](const auto& point) {
                return std::abs(point.x() - frequency) < 1.0e-9;
            });
            if (found != series.points.end() && std::isfinite(found->y()))
                rows.push_back(tr("%1: %2 %3")
                    .arg(series.name, formatDecimal(found->y()), series.unit));
        }
        QToolTip::showText(event->globalPosition().toPoint(), rows.join(QLatin1Char('\n')), this);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        auto nearestDistance = 12.0;
        std::optional<double> nearestX;
        for (const auto& series : series_) {
            const auto& axis = series.axis == AxisSide::Left ? leftAxis_ : rightAxis_;
            for (const auto& point : series.points) {
                const auto screenPoint = mapPoint(point, axis);
                if (!std::isfinite(screenPoint.x()) || !std::isfinite(screenPoint.y())) continue;
                const auto distance = std::hypot(screenPoint.x() - event->position().x(),
                    screenPoint.y() - event->position().y());
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearestX = point.x();
                }
            }
        }
        if (nearestX && xActivatedCallback_) xActivatedCallback_(*nearestX);
    }

    void leaveEvent(QEvent*) override
    {
        clearHover();
        QToolTip::hideText();
        update();
    }

private:
    void rebuildAxes()
    {
        const auto text = palette().color(QPalette::Text);
        const auto resistanceColor = palette().color(QPalette::Highlight);
        const auto reactanceColor = palette().color(QPalette::Link);
        if (plotKind_ == PlotKind::Impedance) {
            leftAxis_ = makeLogAxis(tr("R [Ω]"), resistanceColor,
                collectValues(series_, AxisSide::Left), TickStyle::Resistance);
            rightAxis_ = makeLinearAxis(tr("X [Ω]"), reactanceColor,
                collectValues(series_, AxisSide::Right), true);
        } else if (plotKind_ == PlotKind::Linear) {
            leftAxis_ = makeLinearAxis(leftAxisLabel_, text,
                collectValues(series_, AxisSide::Left), true);
            rightAxis_ = {};
        } else if (swrScaleMode_ == SwrScaleMode::Logarithmic) {
            leftAxis_ = makeLogAxis(leftAxisLabel_, text,
                collectValues(series_, AxisSide::Left), TickStyle::Swr, 1.0);
            rightAxis_ = {};
        } else if (swrScaleMode_ == SwrScaleMode::LimitThree
            || swrScaleMode_ == SwrScaleMode::LimitFive) {
            const auto maximum = swrScaleMode_ == SwrScaleMode::LimitThree ? 3.0 : 5.0;
            leftAxis_ = makeLinearAxis(leftAxisLabel_, text,
                collectValues(series_, AxisSide::Left), false, {{1.0, maximum}});
            rightAxis_ = {};
        } else {
            leftAxis_ = makeLinearAxis(leftAxisLabel_, text,
                collectValues(series_, AxisSide::Left), false);
            leftAxis_.minimum = std::max(1.0, leftAxis_.minimum);
            rightAxis_ = {};
        }
        calculateXBounds();
        boundsValid_ = leftAxis_.valid
            && (plotKind_ != PlotKind::Impedance || rightAxis_.valid)
            && std::isfinite(xMinimum_) && xMaximum_ > xMinimum_;
    }

    void calculateXBounds()
    {
        xMinimum_ = std::numeric_limits<double>::infinity();
        xMaximum_ = -std::numeric_limits<double>::infinity();
        for (const auto& series : series_) {
            for (const auto& point : series.points) {
                if (!std::isfinite(point.x())) continue;
                xMinimum_ = std::min(xMinimum_, point.x());
                xMaximum_ = std::max(xMaximum_, point.x());
            }
        }
        if (xMinimum_ == xMaximum_) {
            const auto padding = std::max(0.1, std::abs(xMinimum_) * 0.01);
            xMinimum_ -= padding;
            xMaximum_ += padding;
        }
    }

    void drawLegend(QPainter& painter)
    {
        auto legendX = plotRect_.left();
        const auto legendY = 48.0;
        for (const auto& series : series_) {
            painter.setPen(QPen(series.color, 2));
            painter.drawLine(QPointF(legendX, legendY), QPointF(legendX + 18, legendY));
            painter.setPen(palette().color(QPalette::Text));
            const auto textWidth = painter.fontMetrics().horizontalAdvance(series.name) + 30;
            painter.drawText(QRectF(legendX + 23, legendY - 9, textWidth, 18),
                Qt::AlignLeft | Qt::AlignVCenter, series.name);
            legendX += textWidth + 30;
        }
    }

    void drawAxes(QPainter& painter)
    {
        const auto gridColor = palette().color(QPalette::Mid);
        constexpr auto xTickCount = 5;
        painter.setPen(QPen(gridColor, 1, Qt::DotLine));
        for (auto tick = 0; tick <= xTickCount; ++tick) {
            const auto fraction = static_cast<double>(tick) / xTickCount;
            const auto x = plotRect_.left() + fraction * plotRect_.width();
            painter.drawLine(QPointF(x, plotRect_.top()), QPointF(x, plotRect_.bottom()));
        }
        for (const auto& tick : leftAxis_.ticks) {
            const auto y = plotRect_.bottom() - tick.position * plotRect_.height();
            painter.drawLine(QPointF(plotRect_.left(), y), QPointF(plotRect_.right(), y));
        }

        painter.setPen(palette().color(QPalette::Text));
        painter.drawRect(plotRect_);
        for (auto tick = 0; tick <= xTickCount; ++tick) {
            const auto fraction = static_cast<double>(tick) / xTickCount;
            const auto xValue = xMinimum_ + fraction * (xMaximum_ - xMinimum_);
            const auto x = plotRect_.left() + fraction * plotRect_.width();
            painter.drawText(QRectF(x - 42, plotRect_.bottom() + 5, 84, 20),
                Qt::AlignHCenter | Qt::AlignTop, formatAxisValue(xValue));
        }
        drawYAxis(painter, leftAxis_, AxisSide::Left);
        if (plotKind_ == PlotKind::Impedance)
            drawYAxis(painter, rightAxis_, AxisSide::Right);
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(QRectF(plotRect_.left(), height() - 25, plotRect_.width(), 20),
            Qt::AlignCenter, xAxisLabel_);
    }

    void drawYAxis(QPainter& painter, const PlotAxis& axis, AxisSide side)
    {
        painter.setPen(axis.color);
        for (const auto& tick : axis.ticks) {
            const auto y = plotRect_.bottom() - tick.position * plotRect_.height();
            const auto labelRect = side == AxisSide::Left
                ? QRectF(24, y - 10, plotRect_.left() - 34, 20)
                : QRectF(plotRect_.right() + 9, y - 10, width() - plotRect_.right() - 30, 20);
            painter.drawText(labelRect,
                (side == AxisSide::Left ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                formatAxisValue(tick.value));
        }
        painter.save();
        if (side == AxisSide::Left) {
            painter.translate(15, plotRect_.center().y());
            painter.rotate(-90);
        } else {
            painter.translate(width() - 15, plotRect_.center().y());
            painter.rotate(90);
        }
        painter.drawText(QRectF(-plotRect_.height() / 2, -11, plotRect_.height(), 22),
            Qt::AlignCenter, axis.label);
        painter.restore();
    }

    void drawSeries(QPainter& painter)
    {
        for (auto seriesIndex = 0; seriesIndex < static_cast<int>(series_.size()); ++seriesIndex) {
            const auto& series = series_[seriesIndex];
            const auto& axis = series.axis == AxisSide::Left ? leftAxis_ : rightAxis_;
            QPainterPath path;
            bool started{};
            painter.setPen(QPen(series.color, 2));
            painter.setBrush(series.color);
            for (auto pointIndex = 0; pointIndex < static_cast<int>(series.points.size()); ++pointIndex) {
                const auto screenPoint = mapPoint(series.points[pointIndex], axis);
                if (!std::isfinite(screenPoint.x()) || !std::isfinite(screenPoint.y())) {
                    started = false;
                    continue;
                }
                if (!started) {
                    path.moveTo(screenPoint);
                    started = true;
                } else {
                    path.lineTo(screenPoint);
                }
                const auto radius = seriesIndex == hoveredSeries_ && pointIndex == hoveredPoint_
                    ? 5.0 : 3.0;
                painter.drawEllipse(screenPoint, radius, radius);
            }
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
    }

    void drawSelectedFrequency(QPainter& painter)
    {
        if (!std::isfinite(selectedFrequencyMHz_)
            || selectedFrequencyMHz_ < xMinimum_ || selectedFrequencyMHz_ > xMaximum_) return;
        const auto x = plotRect_.left()
            + (selectedFrequencyMHz_ - xMinimum_) / (xMaximum_ - xMinimum_) * plotRect_.width();
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
        painter.drawLine(QPointF(x, plotRect_.top()), QPointF(x, plotRect_.bottom()));
        painter.drawText(QRectF(x - 75, plotRect_.top() + 8, 150, 20), Qt::AlignCenter,
            selectedCaption_);
    }

    [[nodiscard]] auto mapPoint(const QPointF& point, const PlotAxis& axis) const -> QPointF
    {
        const auto yPosition = axisPosition(point.y(), axis);
        if (!std::isfinite(point.x()) || !std::isfinite(yPosition))
            return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
        return {
            plotRect_.left() + (point.x() - xMinimum_) / (xMaximum_ - xMinimum_) * plotRect_.width(),
            plotRect_.bottom() - std::clamp(yPosition, 0.0, 1.0) * plotRect_.height(),
        };
    }

    void setPlotProperties()
    {
        setProperty("plotLeftMargin", plotRect_.left());
        setProperty("plotRightMargin", width() - plotRect_.right());
        setProperty("leftAxisScale", static_cast<int>(leftAxis_.scale));
        setProperty("leftAxisMinimum", leftAxis_.minimum);
        setProperty("leftAxisMaximum", leftAxis_.maximum);
        setProperty("leftAxisTickCount", static_cast<int>(leftAxis_.ticks.size()));
        setProperty("rightAxisScale", static_cast<int>(rightAxis_.scale));
        setProperty("rightAxisMinimum", rightAxis_.minimum);
        setProperty("rightAxisMaximum", rightAxis_.maximum);
        setProperty("rightAxisTickCount", static_cast<int>(rightAxis_.ticks.size()));
        setProperty("dualAxes", plotKind_ == PlotKind::Impedance);
    }

    void clearHover()
    {
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
    }

    QString title_;
    QString xAxisLabel_{tr("Frequency (MHz)")};
    QString leftAxisLabel_;
    QString selectedCaption_;
    std::vector<PlotSeries> series_;
    PlotAxis leftAxis_;
    PlotAxis rightAxis_;
    QRectF plotRect_;
    double xMinimum_{};
    double xMaximum_{};
    double selectedFrequencyMHz_{std::numeric_limits<double>::quiet_NaN()};
    SwrScaleMode swrScaleMode_{SwrScaleMode::Logarithmic};
    PlotKind plotKind_{PlotKind::Swr};
    bool boundsValid_{};
    int hoveredSeries_{-1};
    int hoveredPoint_{-1};
    std::function<void(double)> xActivatedCallback_;
};

CandidatePlotsView::CandidatePlotsView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("optimizationCandidatePlots"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* instructions = new QLabel(tr(
        "Objective score and its weighted contributions by candidate. Lower is better; double-click a marker for frequency details."), this);
    instructions->setWordWrap(true);
    scorePlot_ = new SweepPlotWidget(this);
    scorePlot_->setObjectName(QStringLiteral("optimizationCandidateScorePlot"));
    scorePlot_->setXActivatedCallback([this](double value) {
        const auto candidate = std::ranges::find_if(candidates_, [value](const auto& point) {
            return std::abs(point.value - value) < 1.0e-9;
        });
        if (candidate != candidates_.end() && candidateActivatedCallback_)
            candidateActivatedCallback_(candidate->row);
    });
    layout->addWidget(instructions);
    layout->addWidget(scorePlot_, 1);
    clear();
}

void CandidatePlotsView::setCandidates(QString variableName, QString valueSuffix,
    const std::vector<CandidatePlotPoint>& candidates, int bestRow)
{
    candidates_ = candidates;
    std::ranges::sort(candidates_, {}, &CandidatePlotPoint::value);
    const auto scoreColor = palette().color(QPalette::Highlight);
    const auto swrColor = palette().color(QPalette::Link);
    const auto resistanceColor = palette().color(QPalette::Mid);
    const auto reactanceColor = palette().color(QPalette::Text);
    std::vector<PlotSeries> series{
        {tr("Objective"), {}, scoreColor, AxisSide::Left, {}},
        {tr("SWR contribution"), {}, swrColor, AxisSide::Left, {}},
        {tr("R contribution"), {}, resistanceColor, AxisSide::Left, {}},
        {tr("X contribution"), {}, reactanceColor, AxisSide::Left, {}},
    };
    std::optional<double> bestValue;
    for (const auto& candidate : candidates_) {
        series[0].points.emplace_back(candidate.value, candidate.evaluation.score);
        series[1].points.emplace_back(candidate.value, candidate.evaluation.swrComponent);
        series[2].points.emplace_back(candidate.value, candidate.evaluation.resistanceComponent);
        series[3].points.emplace_back(candidate.value, candidate.evaluation.reactanceComponent);
        if (candidate.row == bestRow) bestValue = candidate.value;
    }
    series.erase(std::remove_if(series.begin() + 1, series.end(), [](const auto& item) {
        return std::ranges::all_of(item.points, [](const auto& point) {
            return std::abs(point.y()) < 1.0e-15;
        });
    }), series.end());
    const auto xLabel = valueSuffix.isEmpty()
        ? variableName : tr("%1 (%2)").arg(variableName, valueSuffix.trimmed());
    scorePlot_->setLinearPlot(tr("Candidate Objective Breakdown"), xLabel,
        tr("Normalized Objective Score"), std::move(series), bestValue);
}

void CandidatePlotsView::clear()
{
    candidates_.clear();
    scorePlot_->setLinearPlot(tr("Candidate Objective Breakdown"), tr("Candidate Value"),
        tr("Normalized Objective Score"), {});
}

void CandidatePlotsView::setCandidateActivatedCallback(std::function<void(int)> callback)
{
    candidateActivatedCallback_ = std::move(callback);
}

SweepPlotsView::SweepPlotsView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto* heading = new QLabel(tr("Frequency Sweep Plots"), this);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 3);
    heading->setFont(font);
    summary_ = new QLabel(tr("Run an analysis to populate sweep plots."), this);
    summary_->setWordWrap(true);
    auto* splitter = new QSplitter(Qt::Vertical, this);

    impedancePlot_ = new SweepPlotWidget(splitter);
    impedancePlot_->setObjectName(QStringLiteral("impedanceSweepPlot"));

    auto* swrPanel = new QWidget(splitter);
    auto* swrLayout = new QVBoxLayout(swrPanel);
    swrLayout->setContentsMargins(0, 0, 0, 0);
    auto* swrControls = new QHBoxLayout;
    swrControls->addStretch();
    swrControls->addWidget(new QLabel(tr("SWR scale:"), swrPanel));
    swrScaleControl_ = new QComboBox(swrPanel);
    swrScaleControl_->setObjectName(QStringLiteral("swrScaleControl"));
    swrScaleControl_->addItem(tr("Logarithmic"), static_cast<int>(SwrScaleMode::Logarithmic));
    swrScaleControl_->addItem(tr("Linear"), static_cast<int>(SwrScaleMode::Linear));
    swrScaleControl_->addItem(tr("Limit 1–3"), static_cast<int>(SwrScaleMode::LimitThree));
    swrScaleControl_->addItem(tr("Limit 1–5"), static_cast<int>(SwrScaleMode::LimitFive));
    swrControls->addWidget(swrScaleControl_);
    swrLayout->addLayout(swrControls);
    swrPlot_ = new SweepPlotWidget(swrPanel);
    swrPlot_->setObjectName(QStringLiteral("swrSweepPlot"));
    swrLayout->addWidget(swrPlot_, 1);

    splitter->addWidget(impedancePlot_);
    splitter->addWidget(swrPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(heading);
    layout->addWidget(summary_);
    layout->addWidget(splitter, 1);

    QSettings settings;
    swrScaleControl_->setCurrentIndex(std::clamp(
        settings.value(QStringLiteral("results/swrScaleModeV2"), 0).toInt(), 0, 3));
    const auto applySwrScale = [this](int index) {
        const auto mode = static_cast<SwrScaleMode>(swrScaleControl_->itemData(index).toInt());
        swrPlot_->setSwrScaleMode(mode);
        QSettings{}.setValue(QStringLiteral("results/swrScaleModeV2"), index);
    };
    connect(swrScaleControl_, &QComboBox::currentIndexChanged, this, applySwrScale);
    applySwrScale(swrScaleControl_->currentIndex());
}

void SweepPlotsView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{
    std::map<std::pair<int, int>, std::vector<analysis::FeedpointResult>> feedpoints;
    for (const auto& feedpoint : result.feedpoints)
        feedpoints[{feedpoint.wireTag, feedpoint.segment}].push_back(feedpoint);

    std::vector<PlotSeries> impedanceSeries;
    std::vector<PlotSeries> swrSeries;
    const auto resistanceBase = palette().color(QPalette::Highlight);
    const auto reactanceBase = palette().color(QPalette::Link);
    auto colorIndex = 0;
    for (auto& [source, points] : feedpoints) {
        std::ranges::sort(points, {}, &analysis::FeedpointResult::frequencyMHz);
        const auto colorOffset = colorIndex * 18;
        PlotSeries resistance{tr("R W%1:S%2").arg(source.first).arg(source.second), tr("Ω"),
            resistanceBase.lighter(100 + colorOffset), AxisSide::Left, {}};
        PlotSeries reactance{tr("X W%1:S%2").arg(source.first).arg(source.second), tr("Ω"),
            reactanceBase.lighter(100 + colorOffset), AxisSide::Right, {}};
        PlotSeries swr{tr("SWR W%1:S%2").arg(source.first).arg(source.second), {},
            resistanceBase.lighter(100 + colorOffset), AxisSide::Left, {}};
        for (const auto& point : points) {
            resistance.points.emplace_back(point.frequencyMHz, point.impedance.real());
            reactance.points.emplace_back(point.frequencyMHz, point.impedance.imag());
            swr.points.emplace_back(point.frequencyMHz,
                analysis::standingWaveRatio(point.impedance, result.referenceImpedanceOhms));
        }
        impedanceSeries.push_back(std::move(resistance));
        impedanceSeries.push_back(std::move(reactance));
        swrSeries.push_back(std::move(swr));
        ++colorIndex;
    }
    impedancePlot_->setImpedancePlot(std::move(impedanceSeries));
    swrPlot_->setSwrPlot(tr("SWR (%1 Ω)").arg(formatDecimal(result.referenceImpedanceOhms)),
        std::move(swrSeries));
    summary_->setText(result.feedpoints.empty()
            ? tr("No supported feedpoint results were found in %1.").arg(runDirectory)
            : tr("%1 result point(s) from %2. Hover a marker for exact engineering values.")
                .arg(result.feedpoints.size()).arg(runDirectory));
}

void SweepPlotsView::setSelectedFrequency(double frequencyMHz)
{
    impedancePlot_->setSelectedFrequency(frequencyMHz);
    swrPlot_->setSelectedFrequency(frequencyMHz);
}

}
