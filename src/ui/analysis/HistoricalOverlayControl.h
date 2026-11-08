#pragma once

#include <QWidget>

#include <functional>

class QCheckBox;
class QPushButton;

namespace necwb::ui {

class ElidedLabel;

// Picks a historical run to draw dashed behind the current one, and shows or hides it.
class HistoricalOverlayControl final : public QWidget {
public:
    // The checkbox state is remembered under settingsKey.
    explicit HistoricalOverlayControl(QString settingsKey, QWidget* parent = nullptr);

    void setSelection(QString label, QString compatibility);
    void clearSelection();
    void setChecked(bool checked);
    [[nodiscard]] auto isChecked() const -> bool;
    void setVisibilityChangedCallback(std::function<void(bool)> callback);
    void setChooseRequestedCallback(std::function<void(QWidget*)> callback);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // A long run label ends in "…" rather than widening the panel or losing its start.
    void elideSelection();

    QString settingsKey_;
    QString selectionText_;
    QCheckBox* visible_{};
    QPushButton* selection_{};
    ElidedLabel* compatibility_{};
    std::function<void(bool)> visibilityChanged_;
    std::function<void(QWidget*)> chooseRequested_;
};

}
