#include "ui/cards/CardFields.h"

#include <QObject>

#include <algorithm>
#include <array>

namespace necwb::ui {

auto cardFieldChoices(const QString& mnemonic, int fieldIndex) -> std::vector<FieldChoice>
{
    if ((mnemonic == QStringLiteral("SP") || mnemonic == QStringLiteral("SC"))
        && fieldIndex == 1) return {
        {QStringLiteral("0"), QObject::tr("0 — Arbitrary patch / first continuation")},
        {QStringLiteral("1"), QObject::tr("1 — Rectangular patch")},
        {QStringLiteral("2"), QObject::tr("2 — Triangular patch")},
        {QStringLiteral("3"), QObject::tr("3 — Quadrilateral patch")}};
    if (fieldIndex != 0) return {};
    if (mnemonic == QStringLiteral("EX")) return {
        {QStringLiteral("0"), QObject::tr("0 — Applied voltage source")},
        {QStringLiteral("1"), QObject::tr("1 — Linear-polarized plane wave")},
        {QStringLiteral("2"), QObject::tr("2 — Right-hand elliptic plane wave")},
        {QStringLiteral("3"), QObject::tr("3 — Left-hand elliptic plane wave")},
        {QStringLiteral("4"), QObject::tr("4 — Elementary current source")},
        {QStringLiteral("5"), QObject::tr("5 — Current-slope voltage source")}};
    if (mnemonic == QStringLiteral("FR")) return {
        {QStringLiteral("0"), QObject::tr("0 — Linear frequency stepping")},
        {QStringLiteral("1"), QObject::tr("1 — Multiplicative frequency stepping")}};
    if (mnemonic == QStringLiteral("GN")) return {
        {QStringLiteral("-1"), QObject::tr("-1 — Free space / no ground")},
        {QStringLiteral("0"), QObject::tr("0 — Finite ground, reflection approximation")},
        {QStringLiteral("1"), QObject::tr("1 — Perfect conducting ground")},
        {QStringLiteral("2"), QObject::tr("2 — Finite ground, Sommerfeld/Norton")}};
    if (mnemonic == QStringLiteral("GE")) return {
        {QStringLiteral("0"), QObject::tr("0 — No ground-plane connection")},
        {QStringLiteral("1"), QObject::tr("1 — Geometry connected to ground")}};
    if (mnemonic == QStringLiteral("LD")) return {
        {QStringLiteral("-1"), QObject::tr("-1 — Clear all loads")},
        {QStringLiteral("0"), QObject::tr("0 — Series RLC")},
        {QStringLiteral("1"), QObject::tr("1 — Parallel RLC")},
        {QStringLiteral("2"), QObject::tr("2 — Distributed series RLC")},
        {QStringLiteral("3"), QObject::tr("3 — Distributed parallel RLC")},
        {QStringLiteral("4"), QObject::tr("4 — Fixed complex impedance")},
        {QStringLiteral("5"), QObject::tr("5 — Wire conductivity")}};
    return {};
}

auto cardFieldChoiceLabel(const QString& mnemonic, int fieldIndex, const QString& value)
    -> QString
{
    const auto choices = cardFieldChoices(mnemonic, fieldIndex);
    const auto found = std::ranges::find(choices, value, &FieldChoice::value);
    return found == choices.end() ? value : found->label;
}

namespace {

auto labels(const QString& mnemonic, int type) -> QStringList
{
    if (mnemonic == QStringLiteral("EX")) {
        if (type >= 1 && type <= 3)
            return {QObject::tr("Type"), QObject::tr("θ angles"), QObject::tr("φ angles"),
                QObject::tr("Options"), QObject::tr("θ °"), QObject::tr("φ °"),
                QObject::tr("η °"), QObject::tr("Δθ °"), QObject::tr("Δφ °"),
                QObject::tr("Axial ratio")};
        if (type == 4)
            return {QObject::tr("Type"), QObject::tr("Unused"), QObject::tr("Unused"),
                QObject::tr("Options"), QObject::tr("X m"), QObject::tr("Y m"),
                QObject::tr("Z m"), QObject::tr("α °"), QObject::tr("β °"),
                QObject::tr("Moment A·m")};
        return {QObject::tr("Type"), QObject::tr("Wire tag"), QObject::tr("Segment"),
            QObject::tr("Options"), QObject::tr("Real V"), QObject::tr("Imag V")};
    }
    if (mnemonic == QStringLiteral("LD")) {
        const QStringList where{QObject::tr("Type"), QObject::tr("Wire tag"),
            QObject::tr("First segment"), QObject::tr("Last segment")};
        if (type == 4) return where + QStringList{QObject::tr("R Ω"), QObject::tr("X Ω")};
        if (type == 5) return where + QStringList{QObject::tr("σ S/m")};
        if (type == 2 || type == 3)
            return where + QStringList{QObject::tr("R Ω/m"), QObject::tr("L H/m"),
                QObject::tr("C F/m")};
        return where + QStringList{QObject::tr("R Ω"), QObject::tr("L H"), QObject::tr("C F")};
    }
    const QStringList ports{QObject::tr("Wire 1"), QObject::tr("Segment 1"),
        QObject::tr("Wire 2"), QObject::tr("Segment 2")};
    if (mnemonic == QStringLiteral("TL"))
        return ports + QStringList{QObject::tr("Z0 Ω"), QObject::tr("Length m"),
            QObject::tr("G1 S"), QObject::tr("B1 S"), QObject::tr("G2 S"), QObject::tr("B2 S")};
    if (mnemonic == QStringLiteral("NT"))
        return ports + QStringList{QObject::tr("Y11 G S"), QObject::tr("Y11 B S"),
            QObject::tr("Y12 G S"), QObject::tr("Y12 B S"), QObject::tr("Y22 G S"),
            QObject::tr("Y22 B S")};
    return {};
}

}

auto cardFieldLabel(const QString& mnemonic, int fieldIndex, int type) -> QString
{
    return labels(mnemonic, type).value(fieldIndex);
}

auto cardFieldCount(const QString& mnemonic, int type) -> int
{
    return static_cast<int>(labels(mnemonic, type).size());
}

}
