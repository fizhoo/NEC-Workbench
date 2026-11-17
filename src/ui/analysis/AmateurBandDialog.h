#pragma once

#include "analysis/FrequencyPlan.h"

#include <QString>

#include <cstddef>
#include <functional>
#include <vector>

class QWidget;

namespace necwb::ui {

// The wording and object names of one amateur-band chooser; the presets and the
// Select All / Clear / summary / accept behaviour are the same for every one.
struct AmateurBandChoice {
    QString objectName;
    QString title;
    QString description;
    // Each check box is named this with its preset index appended.
    QString checkObjectName;
    QString summaryObjectName;
    QString acceptText;
    QString acceptObjectName;
    std::function<QString(const analysis::AmateurBandPreset&)> label;
    // The summary for the ticked presets, by index.
    std::function<QString(const std::vector<std::size_t>&)> summary;
};

// Shows the amateur-band presets as check boxes and returns the indexes ticked, or none when
// the dialog is cancelled.
[[nodiscard]] auto chooseAmateurBands(QWidget* parent, const AmateurBandChoice& choice)
    -> std::vector<std::size_t>;

}
