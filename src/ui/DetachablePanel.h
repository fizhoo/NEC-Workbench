#pragma once

#include <QString>
#include <QWidget>

class QDialog;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace necwb::ui {

class DetachablePanel final : public QWidget {
public:
    enum class WindowActivation {
        Preserve,
        Activate
    };

    DetachablePanel(QString id, QString title, QWidget* parent = nullptr,
        QString settingsRoot = QStringLiteral("resultWindows"),
        QString windowObjectName = {});
    ~DetachablePanel() override;

    void setContent(QWidget* content);
    [[nodiscard]] auto isDetached() const -> bool;
    [[nodiscard]] auto isWindowVisible() const -> bool;
    [[nodiscard]] auto isWindowActive() const -> bool;
    void showWindow(WindowActivation activation = WindowActivation::Activate);
    void setStatusText(QString status);

private:
    void toggleDetached();
    void detach(bool showWindow = true);
    void attach();
    void showDetached(WindowActivation activation = WindowActivation::Activate);
    void ensureWindowOnScreen();
    void saveWindowGeometry() const;
    void saveWindowState() const;
    [[nodiscard]] auto settingsKey(const QString& name) const -> QString;

    QString id_;
    QString title_;
    QString settingsRoot_;
    QString windowObjectName_;
    QVBoxLayout* layout_{};
    QWidget* content_{};
    QWidget* placeholder_{};
    QPushButton* toggleButton_{};
    QLabel* statusLabel_{};
    QLabel* dialogStatusLabel_{};
    QDialog* dialog_{};
    bool detached_{};
    QString statusText_;
};

}
