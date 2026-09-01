#pragma once

#include <QString>
#include <QWidget>

class QDialog;
class QPushButton;
class QVBoxLayout;

namespace necwb::ui {

class DetachablePanel final : public QWidget {
public:
    DetachablePanel(QString id, QString title, QWidget* parent = nullptr);
    ~DetachablePanel() override;

    void setContent(QWidget* content);
    [[nodiscard]] auto isDetached() const -> bool;

private:
    void toggleDetached();
    void detach();
    void attach();
    void showDetached();
    void saveWindowGeometry() const;

    QString id_;
    QString title_;
    QVBoxLayout* layout_{};
    QWidget* content_{};
    QWidget* placeholder_{};
    QPushButton* toggleButton_{};
    QDialog* dialog_{};
    bool detached_{};
};

}
