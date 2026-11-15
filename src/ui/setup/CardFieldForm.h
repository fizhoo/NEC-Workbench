#pragma once

#include "model/SegmentSharing.h"
#include "nec/NecCard.h"

#include <QWidget>

#include <functional>
#include <vector>

class QFormLayout;
class QLabel;
class QPushButton;

namespace necwb::ui {

// One source (EX), load (LD), transmission line (TL) or network (NT) in the properties pane,
// as a short form. Edits stay pending until Apply. Applying rewrites only the fields edited,
// so the others keep their SY symbols and exact text; a field set by a symbol shows "ƒx" and
// is changed under Variables.
class CardFieldForm final : public QWidget {
public:
    explicit CardFieldForm(QWidget* parent = nullptr);
    ~CardFieldForm() override;

    // shared: the model's shared segments; those the card is on are explained under the fields.
    void setCard(const nec::NecCard& card, const std::vector<model::SharedSegment>& shared = {});
    [[nodiscard]] auto sourceLine() const -> std::size_t { return card_.lineNumber; }
    // What the pane calls the card: "Load", "Transmission line".
    [[nodiscard]] static auto title(const std::string& mnemonic) -> QString;
    [[nodiscard]] auto hasPendingEdits() const noexcept -> bool { return pending_; }
    void discardPendingEdits();
    // Called with the card's line and its new text.
    void setAppliedCallback(std::function<void(std::size_t, const QString&)> callback);
    void setDeleteCallback(std::function<void(std::size_t)> callback);

private:
    struct Field;
    void rebuild();
    void relabel();
    void apply();
    void setPending(bool pending);
    [[nodiscard]] auto type() const -> int;
    [[nodiscard]] auto fieldText(std::size_t index) const -> QString;

    nec::NecCard card_;
    QLabel* heading_{};
    QFormLayout* form_{};
    QLabel* summary_{};
    QLabel* notes_{};
    QLabel* error_{};
    QPushButton* apply_{};
    QPushButton* revert_{};
    QPushButton* delete_{};
    std::vector<Field> fields_;
    std::function<void(std::size_t, const QString&)> applied_;
    std::function<void(std::size_t)> deleted_;
    bool updating_{};
    bool pending_{};
};

}
