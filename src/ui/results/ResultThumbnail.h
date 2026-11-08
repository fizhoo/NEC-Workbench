#pragma once

#include <QFrame>

#include <functional>

class QLabel;

namespace necwb::ui {

class ElidedLabel;

// A titled, clickable frame around a compact result view. When the run has nothing for the
// view, it shrinks to its title and a one-line note.
class ResultThumbnail final : public QFrame {
public:
    ResultThumbnail(QWidget* view, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    // An empty note shows the view; otherwise the note replaces it.
    void setNote(const QString& note);
    void setClickedCallback(std::function<void()> callback);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    ElidedLabel* title_{};
    QLabel* note_{};
    QWidget* view_{};
    std::function<void()> clicked_;
};

}
