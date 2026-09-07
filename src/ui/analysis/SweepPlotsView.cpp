#include "ui/analysis/SweepPlotsView.h"

#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSplitter>
#include <QSettings>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace necwb::ui {
namespace {

struct PlotSeries {
    QString name;
    QColor color;
    std::vector<QPointF> points;
};

enum class ScaleMode {
    AutoLinear,
    Logarithmic,
    LimitThree,
    LimitFive,
};

auto formatAxisValue(double value) -> QString
{
    return formatDecimal(value);
}

auto wholeNumberTickStep(double range) -> double
{
    const auto rawStep = std::max(1.0, range / 5.0);
    const auto magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const auto normalized = rawStep / magnitude;
    const auto multiplier = normalized <= 1.0 ? 1.0
        : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
    return std::max(1.0, multiplier * magnitude);
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

    void setPlot(QString title, QString yAxisLabel, std::vector<PlotSeries> series,
        bool includeZero, double minimumY = -std::numeric_limits<double>::infinity())
    {
        title_ = std::move(title);
        yAxisLabel_ = std::move(yAxisLabel);
        series_ = std::move(series);
        includeZero_ = includeZero;
        minimumY_ = minimumY;
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        update();
    }

    void setSelectedFrequency(double frequencyMHz)
    {
        selectedFrequencyMHz_ = frequencyMHz;
        update();
    }

    void setScaleMode(ScaleMode mode)
    {
        scaleMode_ = mode;
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        setProperty("scaleMode", static_cast<int>(mode));
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
        painter.drawText(QRectF(14, 8, width() - 28, 24), Qt::AlignLeft | Qt::AlignVCenter, title_);
        titleFont.setBold(false);
        painter.setFont(titleFont);

        plotRect_ = QRectF(110, 44, std::max(1, width() - 132), std::max(1, height() - 92));
        if (!calculateBounds()) {
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawText(plotRect_, Qt::AlignCenter,
                tr("Run a frequency analysis to populate this plot."));
            return;
        }

        const auto gridColor = palette().color(QPalette::Mid);
        painter.setPen(QPen(gridColor, 1, Qt::DotLine));
        constexpr auto xTickCount = 5;
        for (auto tick = 0; tick <= xTickCount; ++tick) {
            const auto fraction = static_cast<double>(tick) / xTickCount;
            const auto x = plotRect_.left() + fraction * plotRect_.width();
            painter.drawLine(QPointF(x, plotRect_.top()), QPointF(x, plotRect_.bottom()));
        }
        for (auto tick = 0; tick <= yTickCount_; ++tick) {
            const auto fraction = static_cast<double>(tick) / yTickCount_;
            const auto y = plotRect_.bottom() - fraction * plotRect_.height();
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
        for (auto tick = 0; tick <= yTickCount_; ++tick) {
            const auto fraction = static_cast<double>(tick) / yTickCount_;
            const auto transformedValue = yMinimum_ + tick * yTickStep_;
            const auto yValue = inverseY(transformedValue);
            const auto y = plotRect_.bottom() - fraction * plotRect_.height();
            painter.drawText(QRectF(34, y - 10, plotRect_.left() - 46, 20),
                Qt::AlignRight | Qt::AlignVCenter,
                scaleMode_ == ScaleMode::Logarithmic
                    ? formatAxisValue(yValue)
                    : QString::number(static_cast<qlonglong>(std::llround(yValue))));
        }
        painter.drawText(QRectF(plotRect_.left(), height() - 25, plotRect_.width(), 20),
            Qt::AlignCenter, tr("Frequency (MHz)"));
        painter.save();
        painter.translate(15, plotRect_.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-plotRect_.height() / 2, -11, plotRect_.height(), 22),
            Qt::AlignCenter, yAxisLabel_);
        painter.restore();

        auto legendX = plotRect_.left() + 8;
        const auto legendY = plotRect_.top() + 8;
        for (const auto& series : series_) {
            painter.setPen(QPen(series.color, 2));
            painter.drawLine(QPointF(legendX, legendY), QPointF(legendX + 18, legendY));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(QRectF(legendX + 23, legendY - 9, 115, 18),
                Qt::AlignLeft | Qt::AlignVCenter, series.name);
            legendX += 145;
        }

        for (auto seriesIndex = 0; seriesIndex < static_cast<int>(series_.size()); ++seriesIndex) {
            const auto& series = series_[seriesIndex];
            QPainterPath path;
            bool started{};
            painter.setPen(QPen(series.color, 2));
            painter.setBrush(series.color);
            for (auto pointIndex = 0; pointIndex < static_cast<int>(series.points.size()); ++pointIndex) {
                const auto& point = series.points[pointIndex];
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                    started = false;
                    continue;
                }
                const auto screenPoint = mapPoint(point);
                const auto clippedHigh = transformedY(point.y()) > yMaximum_;
                const auto clippedLow = transformedY(point.y()) < yMinimum_;
                if (!started) {
                    path.moveTo(screenPoint);
                    started = true;
                } else {
                    path.lineTo(screenPoint);
                }
                const auto radius = seriesIndex == hoveredSeries_ && pointIndex == hoveredPoint_ ? 5.0 : 3.0;
                painter.drawEllipse(screenPoint, radius, radius);
                if (clippedHigh || clippedLow) {
                    QPolygonF marker;
                    const auto direction = clippedHigh ? 1.0 : -1.0;
                    marker << QPointF(screenPoint.x(), screenPoint.y())
                           << QPointF(screenPoint.x() - 5, screenPoint.y() + direction * 7)
                           << QPointF(screenPoint.x() + 5, screenPoint.y() + direction * 7);
                    painter.drawPolygon(marker);
                }
            }
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }
        if (std::isfinite(selectedFrequencyMHz_)
            && selectedFrequencyMHz_ >= xMinimum_ && selectedFrequencyMHz_ <= xMaximum_) {
            const auto x = mapPoint({selectedFrequencyMHz_, yMinimum_}).x();
            painter.setPen(QPen(QColor(225, 145, 35), 2, Qt::DashLine));
            painter.drawLine(QPointF(x, plotRect_.top()), QPointF(x, plotRect_.bottom()));
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(QRectF(x - 60, plotRect_.top() + 27, 120, 20), Qt::AlignCenter,
                tr("Selected %1 MHz").arg(formatDecimal(selectedFrequencyMHz_)));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        auto nearestDistance = 12.0;
        auto nearestSeries = -1;
        auto nearestPoint = -1;
        if (boundsValid_) {
            for (auto seriesIndex = 0; seriesIndex < static_cast<int>(series_.size()); ++seriesIndex) {
                for (auto pointIndex = 0;
                     pointIndex < static_cast<int>(series_[seriesIndex].points.size()); ++pointIndex) {
                    const auto& point = series_[seriesIndex].points[pointIndex];
                    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                        continue;
                    }
                    const auto screenPoint = mapPoint(point);
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
        if (hoveredSeries_ >= 0) {
            const auto& series = series_[hoveredSeries_];
            const auto& point = series.points[hoveredPoint_];
            QToolTip::showText(event->globalPosition().toPoint(),
                tr("%1\n%2 MHz\n%3")
                    .arg(series.name, formatAxisValue(point.x()), formatAxisValue(point.y())), this);
        } else {
            QToolTip::hideText();
        }
    }

    void leaveEvent(QEvent*) override
    {
        hoveredSeries_ = -1;
        hoveredPoint_ = -1;
        QToolTip::hideText();
        update();
    }

private:
    auto calculateBounds() -> bool
    {
        xMinimum_ = std::numeric_limits<double>::infinity();
        xMaximum_ = -std::numeric_limits<double>::infinity();
        yMinimum_ = std::numeric_limits<double>::infinity();
        yMaximum_ = -std::numeric_limits<double>::infinity();
        for (const auto& series : series_) {
            for (const auto& point : series.points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                    continue;
                }
                xMinimum_ = std::min(xMinimum_, point.x());
                xMaximum_ = std::max(xMaximum_, point.x());
                const auto transformed = transformedY(point.y());
                if (!std::isfinite(transformed)) continue;
                yMinimum_ = std::min(yMinimum_, transformed);
                yMaximum_ = std::max(yMaximum_, transformed);
            }
        }
        boundsValid_ = std::isfinite(xMinimum_) && std::isfinite(yMinimum_);
        if (!boundsValid_) {
            return false;
        }
        if (includeZero_) {
            yMinimum_ = std::min(transformedY(0.0), yMinimum_);
            yMaximum_ = std::max(transformedY(0.0), yMaximum_);
        }
        const auto transformedMinimum = transformedY(minimumY_);
        yMinimum_ = std::max(yMinimum_, transformedMinimum);
        if (xMinimum_ == xMaximum_) {
            const auto padding = std::max(0.1, std::abs(xMinimum_) * 0.01);
            xMinimum_ -= padding;
            xMaximum_ += padding;
        }
        if (scaleMode_ == ScaleMode::LimitThree || scaleMode_ == ScaleMode::LimitFive) {
            yMinimum_ = transformedMinimum;
            yMaximum_ = scaleMode_ == ScaleMode::LimitThree ? 3.0 : 5.0;
            yTickStep_ = 1.0;
            yTickCount_ = static_cast<int>(yMaximum_ - yMinimum_);
            setPlotProperties();
            return true;
        }
        if (yMinimum_ == yMaximum_) {
            const auto padding = std::max(0.1, std::abs(yMinimum_) * 0.05);
            yMinimum_ = std::max(transformedMinimum, yMinimum_ - padding);
            yMaximum_ += padding;
        } else {
            const auto padding = (yMaximum_ - yMinimum_) * 0.08;
            yMinimum_ = std::max(transformedMinimum, yMinimum_ - padding);
            yMaximum_ += padding;
        }
        if (scaleMode_ == ScaleMode::Logarithmic) {
            yMinimum_ = std::floor(yMinimum_);
            yMaximum_ = std::ceil(yMaximum_);
            if (yMaximum_ <= yMinimum_) yMaximum_ = yMinimum_ + 1.0;
            yTickCount_ = 5;
            yTickStep_ = (yMaximum_ - yMinimum_) / yTickCount_;
            setPlotProperties();
            return true;
        }
        yTickStep_ = wholeNumberTickStep(yMaximum_ - yMinimum_);
        if (std::isfinite(minimumY_)) {
            yMinimum_ = transformedMinimum;
            yTickCount_ = std::max(1, static_cast<int>(std::ceil(
                (yMaximum_ - yMinimum_) / yTickStep_)));
            yMaximum_ = yMinimum_ + yTickCount_ * yTickStep_;
        } else {
            yMinimum_ = std::floor(yMinimum_ / yTickStep_) * yTickStep_;
            yMaximum_ = std::ceil(yMaximum_ / yTickStep_) * yTickStep_;
            if (yMaximum_ <= yMinimum_) yMaximum_ = yMinimum_ + yTickStep_;
            yTickCount_ = std::max(1, static_cast<int>(std::llround(
                (yMaximum_ - yMinimum_) / yTickStep_)));
        }
        setPlotProperties();
        return true;
    }

    void setPlotProperties()
    {
        setProperty("yTickStep", yTickStep_);
        setProperty("plotLeftMargin", plotRect_.left());
        setProperty("yMinimum", inverseY(yMinimum_));
        setProperty("yMaximum", inverseY(yMaximum_));
    }

    [[nodiscard]] auto transformedY(double value) const -> double
    {
        if (scaleMode_ != ScaleMode::Logarithmic) return value;
        if (includeZero_) return std::copysign(std::log10(1.0 + std::abs(value)), value);
        return value > 0.0 ? std::log10(value) : std::numeric_limits<double>::quiet_NaN();
    }

    [[nodiscard]] auto inverseY(double value) const -> double
    {
        if (scaleMode_ != ScaleMode::Logarithmic) return value;
        if (includeZero_) return std::copysign(std::pow(10.0, std::abs(value)) - 1.0, value);
        return std::pow(10.0, value);
    }

    [[nodiscard]] auto mapPoint(const QPointF& point) const -> QPointF
    {
        return {
            plotRect_.left() + (point.x() - xMinimum_) / (xMaximum_ - xMinimum_) * plotRect_.width(),
            plotRect_.bottom() - (std::clamp(transformedY(point.y()), yMinimum_, yMaximum_)
                - yMinimum_) / (yMaximum_ - yMinimum_) * plotRect_.height(),
        };
    }

    QString title_;
    QString yAxisLabel_;
    std::vector<PlotSeries> series_;
    QRectF plotRect_;
    double xMinimum_{};
    double xMaximum_{};
    double yMinimum_{};
    double yMaximum_{};
    double yTickStep_{1.0};
    double minimumY_{-std::numeric_limits<double>::infinity()};
    double selectedFrequencyMHz_{std::numeric_limits<double>::quiet_NaN()};
    bool includeZero_{};
    ScaleMode scaleMode_{ScaleMode::AutoLinear};
    bool boundsValid_{};
    int yTickCount_{1};
    int hoveredSeries_{-1};
    int hoveredPoint_{-1};
};

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
    auto* impedancePanel = new QWidget(splitter);
    auto* impedanceLayout = new QVBoxLayout(impedancePanel);
    impedanceLayout->setContentsMargins(0, 0, 0, 0);
    auto* impedanceControls = new QHBoxLayout;
    impedanceControls->addStretch();
    impedanceControls->addWidget(new QLabel(tr("Scale:"), impedancePanel));
    impedanceScaleControl_ = new QComboBox(impedancePanel);
    impedanceScaleControl_->setObjectName(QStringLiteral("impedanceScaleControl"));
    impedanceScaleControl_->addItem(tr("Auto Linear"), static_cast<int>(ScaleMode::AutoLinear));
    impedanceScaleControl_->addItem(tr("Symmetric Log"), static_cast<int>(ScaleMode::Logarithmic));
    impedanceControls->addWidget(impedanceScaleControl_);
    impedanceLayout->addLayout(impedanceControls);
    impedancePlot_ = new SweepPlotWidget(impedancePanel);
    impedancePlot_->setObjectName(QStringLiteral("impedanceSweepPlot"));
    impedanceLayout->addWidget(impedancePlot_, 1);

    auto* swrPanel = new QWidget(splitter);
    auto* swrLayout = new QVBoxLayout(swrPanel);
    swrLayout->setContentsMargins(0, 0, 0, 0);
    auto* swrControls = new QHBoxLayout;
    swrControls->addStretch();
    swrControls->addWidget(new QLabel(tr("Scale:"), swrPanel));
    swrScaleControl_ = new QComboBox(swrPanel);
    swrScaleControl_->setObjectName(QStringLiteral("swrScaleControl"));
    swrScaleControl_->addItem(tr("Auto Linear"), static_cast<int>(ScaleMode::AutoLinear));
    swrScaleControl_->addItem(tr("Logarithmic"), static_cast<int>(ScaleMode::Logarithmic));
    swrScaleControl_->addItem(tr("Limit 1–3"), static_cast<int>(ScaleMode::LimitThree));
    swrScaleControl_->addItem(tr("Limit 1–5"), static_cast<int>(ScaleMode::LimitFive));
    swrControls->addWidget(swrScaleControl_);
    swrLayout->addLayout(swrControls);
    swrPlot_ = new SweepPlotWidget(swrPanel);
    swrPlot_->setObjectName(QStringLiteral("swrSweepPlot"));
    swrLayout->addWidget(swrPlot_, 1);
    splitter->addWidget(impedancePanel);
    splitter->addWidget(swrPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(heading);
    layout->addWidget(summary_);
    layout->addWidget(splitter, 1);

    QSettings settings;
    impedanceScaleControl_->setCurrentIndex(std::clamp(
        settings.value(QStringLiteral("results/impedanceScaleMode"), 0).toInt(), 0, 1));
    swrScaleControl_->setCurrentIndex(std::clamp(
        settings.value(QStringLiteral("results/swrScaleMode"), 0).toInt(), 0, 3));
    const auto applyImpedanceScale = [this](int index) {
        const auto mode = static_cast<ScaleMode>(impedanceScaleControl_->itemData(index).toInt());
        impedancePlot_->setScaleMode(mode);
        QSettings{}.setValue(QStringLiteral("results/impedanceScaleMode"), index);
    };
    const auto applySwrScale = [this](int index) {
        const auto mode = static_cast<ScaleMode>(swrScaleControl_->itemData(index).toInt());
        swrPlot_->setScaleMode(mode);
        QSettings{}.setValue(QStringLiteral("results/swrScaleMode"), index);
    };
    connect(impedanceScaleControl_, &QComboBox::currentIndexChanged,
        this, applyImpedanceScale);
    connect(swrScaleControl_, &QComboBox::currentIndexChanged, this, applySwrScale);
    applyImpedanceScale(impedanceScaleControl_->currentIndex());
    applySwrScale(swrScaleControl_->currentIndex());
}

void SweepPlotsView::setResults(const analysis::AnalysisResult& result, const QString& runDirectory)
{
    std::map<std::pair<int, int>, std::vector<analysis::FeedpointResult>> feedpoints;
    for (const auto& feedpoint : result.feedpoints) {
        feedpoints[{feedpoint.wireTag, feedpoint.segment}].push_back(feedpoint);
    }

    std::vector<PlotSeries> impedanceSeries;
    std::vector<PlotSeries> swrSeries;
    auto colorIndex = 0;
    for (auto& [source, points] : feedpoints) {
        std::ranges::sort(points, {}, &analysis::FeedpointResult::frequencyMHz);
        const auto hue = (210 + colorIndex * 83) % 360;
        PlotSeries resistance{tr("R W%1:S%2").arg(source.first).arg(source.second),
            QColor::fromHsv(hue, 190, 220), {}};
        PlotSeries reactance{tr("X W%1:S%2").arg(source.first).arg(source.second),
            QColor::fromHsv((hue + 145) % 360, 190, 220), {}};
        PlotSeries swr{tr("W%1:S%2").arg(source.first).arg(source.second),
            QColor::fromHsv((115 + colorIndex * 83) % 360, 190, 205), {}};
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
    impedancePlot_->setPlot(tr("Feedpoint Resistance and Reactance"), tr("Impedance (Ω)"),
        std::move(impedanceSeries), true);
    swrPlot_->setPlot(tr("Standing-Wave Ratio"),
        tr("SWR (%1 Ω)").arg(formatDecimal(result.referenceImpedanceOhms)),
        std::move(swrSeries), false, 1.0);
    summary_->setText(result.feedpoints.empty()
            ? tr("No supported feedpoint results were found in %1.").arg(runDirectory)
            : tr("%1 result point(s) from %2. Hover a marker for its exact value.")
                .arg(result.feedpoints.size()).arg(runDirectory));
}

void SweepPlotsView::setSelectedFrequency(double frequencyMHz)
{
    impedancePlot_->setSelectedFrequency(frequencyMHz);
    swrPlot_->setSelectedFrequency(frequencyMHz);
}

}
