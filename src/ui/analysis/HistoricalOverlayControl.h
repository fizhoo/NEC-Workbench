#pragma once

#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QPushButton;

namespace necwb::ui {

class HistoricalOverlayControl final : public QWidget {
public:
    explicit HistoricalOverlayControl(QString id, QWidget* parent = nullptr);

    void setSelection(QString label, QString compatibility);
    void clearSelection();
    void setChecked(bool checked);
    [[nodiscard]] auto isChecked() const -> bool;
    void setVisibilityChangedCallback(std::function<void(bool)> callback);
    void setChooseRequestedCallback(std::function<void(QWidget*)> callback);

private:
    QString settingsKey_;
    QCheckBox* visible_{};
    QPushButton* selection_{};
    QLabel* compatibility_{};
    std::function<void(bool)> visibilityChanged_;
    std::function<void(QWidget*)> chooseRequested_;
};

}
