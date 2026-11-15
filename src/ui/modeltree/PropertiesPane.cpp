#include "ui/modeltree/PropertiesPane.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {

PropertiesPane::PropertiesPane(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("propertiesPane"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(0);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("propertiesPaneTitle"));
    auto font = title_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.9);
    title_->setFont(font);
    title_->setContentsMargins(6, 3, 6, 3);
    auto* header = new QWidget(this);
    header->setBackgroundRole(QPalette::AlternateBase);
    header->setAutoFillBackground(true);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 2, 0);
    headerLayout->setSpacing(0);
    expand_ = new QToolButton(header);
    expand_->setObjectName(QStringLiteral("paneExpandButton"));
    expand_->setText(QStringLiteral("⇔"));
    expand_->setCheckable(true);
    expand_->setAutoRaise(true);
    expand_->setToolTip(tr("Widen the column to show the whole table"));
    expand_->hide();
    headerLayout->addWidget(title_, 1);
    headerLayout->addWidget(expand_);
    connect(expand_, &QToolButton::toggled, this, [this](bool expanded) {
        expand_->setToolTip(expanded ? tr("Narrow the column again")
                                     : tr("Widen the column to show the whole table"));
        if (expanded_) expanded_(expanded);
    });
    pages_ = new QStackedWidget;
    scroll_ = new QScrollArea(this);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setWidget(pages_);
    layout->addWidget(header);
    layout->addWidget(scroll_, 1);
    hide();
}

void PropertiesPane::showPage(QWidget* page, const QString& title)
{
    if (page == nullptr) {
        hide();
        return;
    }
    if (pages_->indexOf(page) < 0) pages_->addWidget(page);
    // Only the page on screen sets the size; a wider hidden page would widen the column.
    for (auto index = 0; index < pages_->count(); ++index) {
        auto* other = pages_->widget(index);
        other->setSizePolicy(other == page
            ? QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred)
            : QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored));
    }
    pages_->setCurrentWidget(page);
    page->show();
    title_->setText(title);
    show();
}

auto PropertiesPane::currentPage() const -> QWidget*
{
    return isHidden() ? nullptr : pages_->currentWidget();
}

auto PropertiesPane::pendingButtons() const -> QList<QPushButton*>
{
    QList<QPushButton*> pending;
    if (auto* page = pages_->currentWidget()) {
        for (auto* button : page->findChildren<QPushButton*>()) {
            if (button->property("pendingChanges").toBool()) pending.append(button);
        }
    }
    return pending;
}

void PropertiesPane::applyPendingEdits()
{
    for (auto* button : pendingButtons()) {
        if (button->isEnabled()) button->click();
    }
}

auto PropertiesPane::commitPendingEdits() -> bool
{
    if (isHidden() || pendingButtons().isEmpty()) return true;
    applyPendingEdits();
    // An edit that is still pending could not be applied: its input is invalid.
    if (pendingButtons().isEmpty()) return true;
    const auto answer = QMessageBox::warning(window(), tr("Invalid Changes"), tr(
        "%1 has changes that cannot be applied as they are.\n\n"
        "Choose Cancel to return and correct them, or discard the edits.").arg(title_->text()),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Discard) {
        focusPage();
        return false;
    }
    if (discard_) discard_(pages_->currentWidget());
    return true;
}

void PropertiesPane::setDiscardCallback(std::function<void(QWidget*)> callback)
{
    discard_ = std::move(callback);
}

void PropertiesPane::focusPage()
{
    auto* page = pages_->currentWidget();
    if (page == nullptr) return;
    // The first field that takes the keyboard, so typing edits the selected item at once.
    for (auto* field : page->findChildren<QWidget*>()) {
        if ((field->focusPolicy() & Qt::TabFocus) && field->isVisibleTo(page)
            && field->isEnabled()) {
            field->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
    page->setFocus(Qt::OtherFocusReason);
}

auto PropertiesPane::revertPendingEdits() -> bool
{
    if (pendingButtons().isEmpty()) return false;
    if (discard_) discard_(pages_->currentWidget());
    return true;
}

void PropertiesPane::setExpandable(bool expandable)
{
    expand_->setVisible(expandable);
}

void PropertiesPane::setExpanded(bool expanded)
{
    const QSignalBlocker blocker(expand_);
    expand_->setChecked(expanded);
}

auto PropertiesPane::isExpanded() const -> bool
{
    return expand_->isChecked();
}

void PropertiesPane::setExpandCallback(std::function<void(bool)> callback)
{
    expanded_ = std::move(callback);
}

void PropertiesPane::keyPressEvent(QKeyEvent* event)
{
    // Fields that do not use these keys themselves pass them up to here.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        applyPendingEdits();
        return;
    }
    if (event->key() == Qt::Key_Escape && revertPendingEdits()) return;
    QWidget::keyPressEvent(event);
}

}
