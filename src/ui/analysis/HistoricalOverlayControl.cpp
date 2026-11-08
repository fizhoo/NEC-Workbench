#include "ui/analysis/HistoricalOverlayControl.h"

#include "ui/ElidedLabel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>

#include <algorithm>
#include <utility>

namespace necwb::ui {

HistoricalOverlayControl::HistoricalOverlayControl(QString settingsKey, QWidget* parent)
    : QWidget(parent), settingsKey_(std::move(settingsKey))
{
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(0);
    visible_ = new QCheckBox(tr("Overlay"), this);
    visible_->setObjectName(QStringLiteral("showHistoricalOverlay"));
    visible_->setToolTip(tr("Draw the chosen historical run dashed behind this one"));
    visible_->setChecked(QSettings{}.value(settingsKey_, false).toBool());
    visible_->setEnabled(false);
    selectionText_ = tr("Choose Overlay…");
    selection_ = new QPushButton(selectionText_, this);
    selection_->setObjectName(QStringLiteral("chooseHistoricalOverlay"));
    // A long run label is clipped rather than widening the panel.
    selection_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    compatibility_ = new ElidedLabel(tr("No overlay selected"), this);
    compatibility_->setObjectName(QStringLiteral("historicalOverlayCompatibility"));
    compatibility_->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(visible_, 0, 0);
    layout->addWidget(selection_, 0, 1);
    layout->addWidget(compatibility_, 1, 0, 1, 2);
    layout->setColumnStretch(1, 1);
    connect(visible_, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings{}.setValue(settingsKey_, checked);
        if (visibilityChanged_) visibilityChanged_(checked);
    });
    connect(selection_, &QPushButton::clicked, this, [this] {
        if (chooseRequested_) chooseRequested_(selection_);
    });
}

void HistoricalOverlayControl::setSelection(QString label, QString compatibility)
{
    selectionText_ = tr("Overlay: %1").arg(label);
    selection_->setToolTip(selectionText_);
    elideSelection();
    compatibility_->setToolTip(compatibility);
    compatibility_->setText(std::move(compatibility));
    visible_->setEnabled(true);
}

void HistoricalOverlayControl::clearSelection()
{
    setChecked(false);
    visible_->setEnabled(false);
    selectionText_ = tr("Choose Overlay…");
    selection_->setToolTip({});
    elideSelection();
    compatibility_->setText(tr("No overlay selected"));
    compatibility_->setToolTip({});
}

void HistoricalOverlayControl::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    elideSelection();
}

void HistoricalOverlayControl::elideSelection()
{
    constexpr auto padding = 16;
    selection_->setText(selection_->fontMetrics().elidedText(
        selectionText_, Qt::ElideRight, std::max(0, selection_->width() - padding)));
}

void HistoricalOverlayControl::setChecked(bool checked)
{
    visible_->setChecked(checked);
}

auto HistoricalOverlayControl::isChecked() const -> bool
{
    return visible_->isChecked();
}

void HistoricalOverlayControl::setVisibilityChangedCallback(
    std::function<void(bool)> callback)
{
    visibilityChanged_ = std::move(callback);
}

void HistoricalOverlayControl::setChooseRequestedCallback(
    std::function<void(QWidget*)> callback)
{
    chooseRequested_ = std::move(callback);
}

}
