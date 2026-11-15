#pragma once

#include <QWidget>

#include <functional>

namespace necwb::ui {

class CenterViewHeader;

// The NEC Deck's raw source shown in the center in place of the geometry, under the same
// ← Geometry bar as a full result view.
class DeckView final : public QWidget {
public:
    DeckView(QWidget* deck, QWidget* parent = nullptr);

    void setBackCallback(std::function<void()> callback);
    // Marks the title while the deck has unsaved changes.
    void setModified(bool modified);

private:
    CenterViewHeader* header_{};
};

}
