#include "ui/results/ResultsColumn.h"

#include "analysis/FrequencyComparison.h"
#include "ui/DisplayFormat.h"
#include "ui/analysis/FieldResultsViews.h"
#include "ui/analysis/HistoricalOverlayControl.h"
#include "ui/analysis/SweepPlotsView.h"
#include "ui/results/KeyNumberStrip.h"
#include "ui/results/ResultThumbnail.h"

#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

constexpr auto MinimumColumnWidth = 280;
constexpr auto DefaultColumnWidth = 360;
// The Smith chart is not built yet; its entry has no view.
constexpr auto NoView = -1;

}

ResultsColumn::ResultsColumn(const ResultState& state, QWidget* parent)
    : QScrollArea(parent), state_(state)
{
    setObjectName(QStringLiteral("resultsColumn"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);
    strip_ = new KeyNumberStrip(content);
    overlay_ = new HistoricalOverlayControl(QStringLiteral("resultsColumn/overlayVisible"), content);
    empty_ = new QLabel(tr("Run analysis to see results"), content);
    empty_->setObjectName(QStringLiteral("resultsColumnEmpty"));
    empty_->setForegroundRole(QPalette::PlaceholderText);
    empty_->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(strip_);
    layout->addWidget(overlay_);
    layout->addWidget(empty_);

    swr_ = new SweepPlotsView;
    swr_->setReferenceControlVisible(false);
    swr_->setCompact(true);
    azimuth_ = new RadiationPatternView;
    azimuth_->setReferenceControlVisible(false);
    azimuth_->setCompact(true, RadiationPatternView::Cut::Azimuth);
    elevation_ = new RadiationPatternView;
    elevation_->setReferenceControlVisible(false);
    elevation_->setCompact(true, RadiationPatternView::Cut::Elevation);
    currents_ = new CurrentDistributionView;
    currents_->setCompact(true);
    const std::array thumbnails{
        std::tuple{&swrThumbnail_, static_cast<QWidget*>(swr_), ResultView::Swr,
            QStringLiteral("swrThumbnail")},
        std::tuple{&azimuthThumbnail_, static_cast<QWidget*>(azimuth_), ResultView::Azimuth,
            QStringLiteral("azimuthThumbnail")},
        std::tuple{&elevationThumbnail_, static_cast<QWidget*>(elevation_),
            ResultView::Elevation, QStringLiteral("elevationThumbnail")},
        std::tuple{&currentsThumbnail_, static_cast<QWidget*>(currents_), ResultView::Currents,
            QStringLiteral("currentsThumbnail")}};
    for (const auto& [thumbnail, view, kind, name] : thumbnails) {
        thumbnail->frame = new ResultThumbnail(view, content);
        thumbnail->frame->setObjectName(name);
        thumbnail->frame->setClickedCallback([this, kind] { if (openView_) openView_(kind); });
        layout->addWidget(thumbnail->frame, 1);
    }

    viewsHeading_ = new QLabel(tr("MORE VIEWS"), content);
    auto headingFont = viewsHeading_->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 0.85);
    headingFont.setBold(true);
    viewsHeading_->setFont(headingFont);
    viewsHeading_->setContentsMargins(4, 2, 4, 0);
    views_ = new QListWidget(content);
    views_->setObjectName(QStringLiteral("resultsMoreViews"));
    views_->setFrameShape(QFrame::NoFrame);
    views_->setUniformItemSizes(true);
    views_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    views_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    views_->setBackgroundRole(QPalette::Window);
    for (const auto& [text, view] : std::initializer_list<std::pair<QString, int>>{
             {tr("Impedance table"), static_cast<int>(ResultView::ImpedanceTable)},
             {tr("Currents along wire"), static_cast<int>(ResultView::CurrentsAlongWire)},
             {tr("Currents table"), static_cast<int>(ResultView::CurrentsTable)},
             {tr("3D pattern"), static_cast<int>(ResultView::Pattern3D)},
             {tr("Performance vs frequency"), static_cast<int>(ResultView::Performance)},
             {tr("Smith chart"), NoView}}) {
        auto* item = new QListWidgetItem(text, views_);
        item->setData(Qt::UserRole, view);
    }
    layout->addWidget(viewsHeading_);
    layout->addWidget(views_);
    connect(views_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const auto view = item->data(Qt::UserRole).toInt();
        if (view != NoView && (item->flags() & Qt::ItemIsEnabled) && openView_)
            openView_(static_cast<ResultView>(view));
    });
    setWidget(content);
    refresh();
}

void ResultsColumn::setOpenViewCallback(std::function<void(ResultView)> callback)
{
    openView_ = std::move(callback);
}

auto ResultsColumn::minimumSizeHint() const -> QSize
{
    return {MinimumColumnWidth, QScrollArea::minimumSizeHint().height()};
}

auto ResultsColumn::sizeHint() const -> QSize
{
    return {DefaultColumnWidth, QScrollArea::sizeHint().height()};
}

void ResultsColumn::refresh()
{
    const auto& result = state_.data->result();
    const auto hasResults = !result.feedpoints.empty() || !result.currents.empty()
        || !result.radiation.empty();
    empty_->setVisible(!hasResults);
    const auto stale = state_.stale ? tr(" · stale") : QString{};
    const auto frequency = state_.frequencyMHz.value_or(0.0);
    const auto atFrequency = [frequency](const auto& values) {
        return std::ranges::any_of(values, [frequency](const auto& value) {
            return nearlyEqual(value.frequencyMHz, frequency);
        });
    };
    const auto update = [&](Thumbnail& thumbnail, auto& view, const QString& title,
                            bool available, const QString& note) {
        thumbnail.frame->setVisible(hasResults);
        thumbnail.frame->setTitle(title + stale);
        if (hasResults && available) applyResultState(view, thumbnail.applied, state_);
        thumbnail.frame->setNote(available ? QString{} : note);
    };
    update(swrThumbnail_, *swr_, tr("SWR · Z0 %1").arg(
               formatReferenceImpedance(state_.referenceImpedanceOhms)),
        !result.feedpoints.empty(), tr("No feedpoint results in this run"));
    // Whether a cut exists depends on the datasets at this frequency, which the view works out.
    const auto hasRadiation = !state_.data->radiationAt(frequency).empty();
    if (hasResults && hasRadiation) {
        applyResultState(*azimuth_, azimuthThumbnail_.applied, state_);
        applyResultState(*elevation_, elevationThumbnail_.applied, state_);
    }
    update(azimuthThumbnail_, *azimuth_, tr("Azimuth pattern"),
        hasRadiation && azimuth_->hasCut(RadiationPatternView::Cut::Azimuth),
        hasRadiation ? tr("No azimuth cut at this frequency")
                     : tr("No radiation pattern at this frequency"));
    update(elevationThumbnail_, *elevation_, tr("Elevation pattern"),
        hasRadiation && elevation_->hasCut(RadiationPatternView::Cut::Elevation),
        hasRadiation ? tr("No elevation cut at this frequency")
                     : tr("No radiation pattern at this frequency"));
    update(currentsThumbnail_, *currents_, tr("Currents"), atFrequency(result.currents),
        tr("No segment currents at this frequency"));
    viewsHeading_->setVisible(hasResults);
    views_->setVisible(hasResults);
    refreshViewList();
}

void ResultsColumn::refreshViewList()
{
    const auto& result = state_.data->result();
    for (auto row = 0; row < views_->count(); ++row) {
        auto* item = views_->item(row);
        const auto view = item->data(Qt::UserRole).toInt();
        auto available = false;
        QString toolTip;
        switch (view) {
        case static_cast<int>(ResultView::ImpedanceTable):
            available = !result.feedpoints.empty();
            break;
        case static_cast<int>(ResultView::CurrentsAlongWire):
        case static_cast<int>(ResultView::CurrentsTable):
            available = !result.currents.empty();
            break;
        case static_cast<int>(ResultView::Pattern3D):
        case static_cast<int>(ResultView::Performance):
            available = !result.radiation.empty();
            break;
        default:
            toolTip = tr("Not available yet");
            break;
        }
        if (toolTip.isEmpty() && !available) toolTip = tr("This run has no data for this view");
        item->setFlags(available ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags);
        item->setToolTip(toolTip);
    }
    views_->setFixedHeight(views_->count() * views_->sizeHintForRow(0) + 2 * views_->frameWidth());
}

}
