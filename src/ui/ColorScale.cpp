#include "ui/ColorScale.h"

#include "ui/DisplayFormat.h"

#include <QPainter>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto BarWidth = 64;

}

auto turboColor(double ratio) -> QColor
{
    // Google's polynomial fit of the Turbo colour map.
    const auto t = std::clamp(ratio, 0.0, 1.0);
    const auto red = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234
        + t * (-152.94239396 + t * 59.28637943))));
    const auto green = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333
        + t * (4.27729857 + t * 2.82956604))));
    const auto blue = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771
        + t * (-89.90310912 + t * 27.34824973))));
    return QColor::fromRgbF(static_cast<float>(std::clamp(red, 0.0, 1.0)),
        static_cast<float>(std::clamp(green, 0.0, 1.0)), static_cast<float>(std::clamp(blue, 0.0, 1.0)));
}

auto currentColor(double magnitude, double maximum) -> QColor
{
    return turboColor(magnitude / std::max(maximum, 1.0e-30));
}

auto turboGradient(const QRectF& bar) -> QLinearGradient
{
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    for (auto index = 0; index <= 4; ++index) {
        const auto ratio = index / 4.0;
        gradient.setColorAt(ratio, turboColor(ratio));
    }
    return gradient;
}

ColorScaleBar::ColorScaleBar(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("colorScaleBar"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void ColorScaleBar::setRange(QString title, double minimum, double maximum, QString unit)
{
    title_ = std::move(title);
    minimum_ = minimum;
    maximum_ = maximum;
    unit_ = std::move(unit);
    setToolTip(tr("%1 from %2 to %3").arg(title_, minimumText(), maximumText()));
    updateGeometry();
    update();
}

auto ColorScaleBar::minimumText() const -> QString
{
    return QStringLiteral("%1 %2").arg(formatDecimal(minimum_), unit_);
}

auto ColorScaleBar::maximumText() const -> QString
{
    return QStringLiteral("%1 %2").arg(formatDecimal(maximum_), unit_);
}

auto ColorScaleBar::sizeHint() const -> QSize
{
    const auto metrics = fontMetrics();
    const auto text = metrics.horizontalAdvance(title_ + QLatin1Char(' ') + minimumText()
        + QLatin1Char(' ') + maximumText());
    return {text + BarWidth + 16, metrics.height()};
}

void ColorScaleBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const auto metrics = fontMetrics();
    painter.setPen(palette().color(QPalette::WindowText));
    auto x = 0;
    const auto drawText = [&](const QString& text) {
        painter.drawText(QRect(x, 0, metrics.horizontalAdvance(text), height()),
            Qt::AlignLeft | Qt::AlignVCenter, text);
        x += metrics.horizontalAdvance(text) + 4;
    };
    drawText(title_);
    drawText(minimumText());
    const QRectF bar(x, (height() - 8) / 2.0, BarWidth, 8);
    painter.fillRect(bar, turboGradient(bar));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(bar);
    painter.setPen(palette().color(QPalette::WindowText));
    x += BarWidth + 4;
    drawText(maximumText());
}

}
