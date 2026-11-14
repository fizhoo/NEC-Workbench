#pragma once

#include "ui/results/ResultState.h"

#include <QScrollArea>

#include <functional>

class QLabel;
class QListWidget;
class QVBoxLayout;

namespace necwb::ui {

class CurrentDistributionView;
class HistoricalOverlayControl;
class KeyNumberStrip;
class RadiationPatternView;
class ResultThumbnail;
class SweepPlotsView;

// The right-hand column of the main window: the key numbers, the historical overlay, four
// live thumbnails and the list of other views. It scrolls rather than squash a thumbnail.
class ResultsColumn final : public QScrollArea {
public:
    ResultsColumn(const ResultState& state, QWidget* parent = nullptr);

    [[nodiscard]] auto strip() const -> KeyNumberStrip* { return strip_; }
    [[nodiscard]] auto overlay() const -> HistoricalOverlayControl* { return overlay_; }
    // Brings the thumbnails up to date with the state.
    void refresh();
    void setOpenViewCallback(std::function<void(ResultView)> callback);

    [[nodiscard]] auto minimumSizeHint() const -> QSize override;
    [[nodiscard]] auto sizeHint() const -> QSize override;

private:
    struct Thumbnail {
        ResultThumbnail* frame{};
        AppliedResultState applied;
    };
    void refreshViewList();

    const ResultState& state_;
    KeyNumberStrip* strip_{};
    HistoricalOverlayControl* overlay_{};
    QLabel* empty_{};
    QVBoxLayout* layout_{};
    SweepPlotsView* swr_{};
    RadiationPatternView* azimuth_{};
    RadiationPatternView* elevation_{};
    CurrentDistributionView* currents_{};
    Thumbnail swrThumbnail_;
    Thumbnail azimuthThumbnail_;
    Thumbnail elevationThumbnail_;
    Thumbnail currentsThumbnail_;
    QLabel* viewsHeading_{};
    QListWidget* views_{};
    std::function<void(ResultView)> openView_;
};

}
