#include "ui/analysis/RadiationSurfaceMesh.h"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace necwb::ui {
namespace {

struct Vector {
    double x{}, y{}, z{};
};

auto operator-(const model::Point3D& a, const model::Point3D& b) -> Vector
{ return {a.x - b.x, a.y - b.y, a.z - b.z}; }

auto cross(const Vector& a, const Vector& b) -> Vector
{ return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

auto dot(const Vector& a, const Vector& b) -> double { return a.x * b.x + a.y * b.y + a.z * b.z; }

auto unit(const Vector& v) -> std::optional<Vector>
{
    const auto length = std::sqrt(dot(v, v));
    if (!(length > 1.0e-12)) return std::nullopt;
    return Vector{v.x / length, v.y / length, v.z / length};
}

// Every `stride`th index of `count`, always with the last.
auto strided(std::size_t count, std::size_t stride) -> std::vector<std::size_t>
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < count; index += stride) indices.push_back(index);
    if (count > 0 && indices.back() != count - 1) indices.push_back(count - 1);
    return indices;
}

// A surface patch, or one piece of a line when `pen` is set.
struct Face {
    QPolygonF polygon;
    double depth{};
    QColor color;
    const QPen* pen{};
};

// Pieces short enough that each sorts on the right side of the patches around it.
constexpr auto LinePieces = 8;

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

auto SurfaceView::project(const model::Point3D& point) const -> QPointF
{
    const auto cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const auto rx = cy * point.x - sy * point.y;
    const auto ry = sy * point.x + cy * point.y;
    return {centre.x() + rx * scale, centre.y() - (cp * point.z - sp * ry) * scale};
}

auto SurfaceView::depth(const model::Point3D& point) const -> double
{
    const auto ry = std::sin(yaw) * point.x + std::cos(yaw) * point.y;
    return std::cos(pitch) * ry + std::sin(pitch) * point.z;
}

auto fitSurfaceView(const std::vector<model::Point3D>& points, double yaw, double pitch,
    const QRectF& area) -> SurfaceView
{
    const SurfaceView unitView{yaw, pitch, 1.0, {}};
    if (points.empty() || area.isEmpty()) return {yaw, pitch, 1.0, area.center()};
    auto low = unitView.project(points.front()), high = low;
    for (const auto& point : points) {
        const auto screen = unitView.project(point);
        low = {std::min(low.x(), screen.x()), std::min(low.y(), screen.y())};
        high = {std::max(high.x(), screen.x()), std::max(high.y(), screen.y())};
    }
    const QRectF bounds(low, high);
    const auto scale = std::min(area.width() / std::max(bounds.width(), 1.0e-6),
        area.height() / std::max(bounds.height(), 1.0e-6));
    return {yaw, pitch, scale, area.center() - bounds.center() * scale};
}

auto drawRadiationSurface(QPainter& painter, const std::vector<SurfaceSample>& samples,
    const SurfaceView& view, bool grid, bool seeThrough, std::size_t maxFaces,
    const std::vector<SurfaceLine>& lines) -> bool
{
    std::map<double, std::size_t> thetaIndex, phiIndex;
    for (const auto& sample : samples) {
        thetaIndex.emplace(sample.thetaDegrees, 0);
        phiIndex.emplace(sample.phiDegrees, 0);
    }
    if (thetaIndex.size() < 2 || phiIndex.size() < 2) return false;
    std::vector<double> phis;
    for (auto index = std::size_t{0}; auto& [theta, slot] : thetaIndex) slot = index++;
    for (auto index = std::size_t{0}; auto& [phi, slot] : phiIndex) {
        slot = index++;
        phis.push_back(phi);
    }
    const auto columns = phiIndex.size();
    std::vector<const SurfaceSample*> cells(thetaIndex.size() * columns);
    for (const auto& sample : samples)
        cells[thetaIndex[sample.thetaDegrees] * columns + phiIndex[sample.phiDegrees]] = &sample;
    // Close the azimuth when it nearly goes round and does not repeat its first column at +360°.
    const auto span = phis.back() - phis.front();
    const auto wraps = span >= 270.0 && span < 359.5;
    auto stride = std::size_t{1};
    const auto faceCount = [&](std::size_t step) {
        return ((thetaIndex.size() - 1) / step + 1) * ((columns - 1) / step + (wraps ? 2 : 1));
    };
    while (faceCount(stride) > maxFaces && stride < columns) ++stride;
    const auto rows = strided(thetaIndex.size(), stride);
    auto cols = strided(columns, stride);
    if (wraps) cols.push_back(cols.front());

    // Lit from above, left and in front, in screen terms (right, into the screen, up).
    const auto light = *unit({-0.45, -0.75, 0.55});
    const auto half = *unit({light.x, light.y - 1.0, light.z});
    const auto cy = std::cos(view.yaw), sy = std::sin(view.yaw);
    const auto cp = std::cos(view.pitch), sp = std::sin(view.pitch);
    const auto toScreen = [&](const Vector& v) {
        const auto ry = sy * v.x + cy * v.y;
        return Vector{cy * v.x - sy * v.y, cp * ry + sp * v.z, cp * v.z - sp * ry};
    };
    std::vector<Face> faces;
    faces.reserve(rows.size() * cols.size());
    for (auto row = std::size_t{1}; row < rows.size(); ++row) {
        for (auto col = std::size_t{1}; col < cols.size(); ++col) {
            const std::array corners{cells[rows[row - 1] * columns + cols[col - 1]],
                cells[rows[row - 1] * columns + cols[col]], cells[rows[row] * columns + cols[col]],
                cells[rows[row] * columns + cols[col - 1]]};
            if (std::ranges::any_of(corners, [](const auto* corner) { return corner == nullptr; }))
                continue;
            // Two sided: a lobe seen from inside is lit like its outside.
            const auto normal = unit(cross(corners[2]->point - corners[0]->point,
                corners[3]->point - corners[1]->point));
            if (!normal) continue;
            const auto facing = toScreen(*normal);
            const auto diffuse = std::abs(dot(facing, light));
            const auto shine = std::pow(std::abs(dot(facing, half)), 14.0) * 0.28;
            auto ratio = 0.0, depth = 0.0;
            Face face;
            for (const auto* corner : corners) {
                face.polygon << view.project(corner->point);
                depth += view.depth(corner->point);
                ratio += corner->colorRatio;
            }
            const auto base = turboColor(ratio / 4.0);
            const auto lit = [&](double channel) {
                return static_cast<float>(std::clamp(channel * (0.32 + 0.68 * diffuse) + shine, 0.0, 1.0));
            };
            face.color = QColor::fromRgbF(lit(base.redF()), lit(base.greenF()), lit(base.blueF()),
                seeThrough ? 0.78F : 1.0F);
            face.depth = depth / 4.0;
            faces.push_back(std::move(face));
        }
    }
    for (const auto& line : lines) {
        const auto at = [&line](double t) {
            return model::Point3D{line.start.x + (line.end.x - line.start.x) * t,
                line.start.y + (line.end.y - line.start.y) * t,
                line.start.z + (line.end.z - line.start.z) * t};
        };
        for (auto piece = 0; piece < LinePieces; ++piece) {
            const auto from = at(double(piece) / LinePieces), to = at(double(piece + 1) / LinePieces);
            faces.push_back({QPolygonF{view.project(from), view.project(to)},
                (view.depth(from) + view.depth(to)) / 2.0 - line.layer * 1.0e-3, {}, &line.pen});
        }
    }
    std::ranges::sort(faces, std::greater{}, &Face::depth);
    painter.save();
    for (const auto& face : faces) {
        if (face.pen != nullptr) {
            painter.setPen(*face.pen);
            painter.drawLine(face.polygon[0], face.polygon[1]);
            continue;
        }
        // The edge in the fill colour closes the hairline gaps antialiasing leaves between patches.
        auto edge = grid ? face.color.darker(160) : face.color;
        if (seeThrough && !grid) edge.setAlphaF(0.25F);
        painter.setPen(QPen(edge, grid ? 0.7 : 0.6));
        painter.setBrush(face.color);
        painter.drawPolygon(face.polygon);
    }
    painter.restore();
    return true;
}

}
