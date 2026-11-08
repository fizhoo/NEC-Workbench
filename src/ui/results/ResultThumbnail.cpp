#include "ui/results/ResultThumbnail.h"

#include "ui/ElidedLabel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

// Below this a thumbnail is unreadable, so the column scrolls instead.
constexpr auto MinimumViewHeight = 100;

}

ResultThumbnail::ResultThumbnail(QWidget* view, QWidget* parent)
    : QFrame(parent), view_(view)
{
    setFrameShape(QFrame::StyledPanel);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 2, 3, 3);
    layout->setSpacing(1);
    title_ = new ElidedLabel(this);
    auto font = title_->font();
    font.setBold(true);
    title_->setFont(font);
    note_ = new QLabel(this);
    note_->setForegroundRole(QPalette::PlaceholderText);
    note_->hide();
    // The thumbnail is a button: the view inside draws but never takes the mouse.
    view_->setParent(this);
    view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    view_->setFocusPolicy(Qt::NoFocus);
    view_->setMinimumHeight(MinimumViewHeight);
    // The column sets the width; the full-size controls hidden inside must not widen it.
    view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(title_);
    layout->addWidget(note_);
    layout->addWidget(view_, 1);
}

void ResultThumbnail::setTitle(const QString& title)
{
    title_->setText(title);
    setToolTip(tr("%1 — click to open it in the center").arg(title));
}

void ResultThumbnail::setNote(const QString& note)
{
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
    view_->setVisible(note.isEmpty());
    setSizePolicy(QSizePolicy::Preferred,
        note.isEmpty() ? QSizePolicy::Expanding : QSizePolicy::Fixed);
}

void ResultThumbnail::setClickedCallback(std::function<void()> callback)
{
    clicked_ = std::move(callback);
}

void ResultThumbnail::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())
        && clicked_)
        clicked_();
}

void ResultThumbnail::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space) && clicked_) {
        clicked_();
        return;
    }
    QFrame::keyPressEvent(event);
}

}
