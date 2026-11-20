#pragma once

#include <QColor>
#include <QLinearGradient>
#include <QRectF>
#include <QString>
#include <QWidget>

namespace necwb::ui {

// Turbo: dark blue at 0 (the floor) through green and yellow to dark red at 1 (the peak). The
// one colour scale for magnitudes: radiation gain, and current on the wires in the 3D pattern
// and the geometry views.
[[nodiscard]] auto turboColor(double ratio) -> QColor;
// A current's colour on that scale, from zero to the largest current.
[[nodiscard]] auto currentColor(double magnitude, double maximum) -> QColor;
// The Turbo scale running left to right across `bar`.
[[nodiscard]] auto turboGradient(const QRectF& bar) -> QLinearGradient;

// One line of scale: "|I| 0 A ▬▬▬ 1.23 A", sized to its text.
class ColorScaleBar final : public QWidget {
public:
    explicit ColorScaleBar(QWidget* parent = nullptr);

    void setRange(QString title, double minimum, double maximum, QString unit);
    [[nodiscard]] auto minimumText() const -> QString;
    [[nodiscard]] auto maximumText() const -> QString;
    [[nodiscard]] auto sizeHint() const -> QSize override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    double minimum_{};
    double maximum_{1.0};
    QString unit_;
};

}
