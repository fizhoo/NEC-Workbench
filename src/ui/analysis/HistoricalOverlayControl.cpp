#include "ui/analysis/HistoricalOverlayControl.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>

#include <utility>

namespace necwb::ui {

HistoricalOverlayControl::HistoricalOverlayControl(QString id, QWidget* parent)
    : QWidget(parent), settingsKey_(QStringLiteral("resultWindows/%1/overlayVisible").arg(id))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);
    visible_ = new QCheckBox(tr("Show Historical Overlay"), this);
    visible_->setObjectName(id + QStringLiteral("ShowHistoricalOverlay"));
    visible_->setChecked(QSettings{}.value(settingsKey_, false).toBool());
    visible_->setEnabled(false);
    selection_ = new QPushButton(tr("Choose Overlay…"), this);
    selection_->setObjectName(id + QStringLiteral("ChooseHistoricalOverlay"));
    selection_->setMaximumWidth(300);
    selection_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    compatibility_ = new QLabel(tr("No overlay selected"), this);
    compatibility_->setObjectName(id + QStringLiteral("HistoricalOverlayCompatibility"));
    layout->addWidget(visible_);
    layout->addWidget(selection_);
    layout->addWidget(compatibility_);
    layout->addStretch();
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
    const auto fullLabel = tr("Overlay: %1").arg(label);
    selection_->setText(fullLabel);
    selection_->setToolTip(fullLabel);
    compatibility_->setText(std::move(compatibility));
    visible_->setEnabled(true);
}

void HistoricalOverlayControl::clearSelection()
{
    setChecked(false);
    visible_->setEnabled(false);
    selection_->setText(tr("Choose Overlay…"));
    selection_->setToolTip({});
    compatibility_->setText(tr("No overlay selected"));
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
