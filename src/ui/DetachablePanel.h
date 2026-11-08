#pragma once

#include <QString>
#include <QSize>
#include <QWidget>

class QDialog;
class QLabel;
class QVBoxLayout;

namespace necwb::ui {

class DetachablePanel final : public QWidget {
public:
    struct Options {
        QString settingsRoot;
        QString windowObjectName;
        QSize defaultWindowSize{900, 680};
    };
    DetachablePanel(QString id, QString title, QWidget* parent, Options options);
    ~DetachablePanel() override;

    void setContent(QWidget* content);
    [[nodiscard]] auto isDetached() const -> bool;
    [[nodiscard]] auto isWindowVisible() const -> bool;
    [[nodiscard]] auto isWindowActive() const -> bool;
    void showWindow();
    void setStatusText(QString status);

private:
    void detach(bool showWindow = true);
    void showDetached();
    void ensureWindowOnScreen();
    void saveWindowGeometry() const;
    void saveWindowState() const;
    [[nodiscard]] auto settingsKey(const QString& name) const -> QString;

    QString id_;
    QString title_;
    QString settingsRoot_;
    QString windowObjectName_;
    QSize defaultWindowSize_;
    QVBoxLayout* layout_{};
    QWidget* content_{};
    QWidget* placeholder_{};
    QLabel* statusLabel_{};
    QLabel* dialogStatusLabel_{};
    QDialog* dialog_{};
    bool detached_{};
    bool windowGeometryInitialized_{};
    mutable bool savedWindowState_{};
    mutable bool savedDetached_{};
    mutable bool savedVisible_{};
    QString statusText_;
};

}
