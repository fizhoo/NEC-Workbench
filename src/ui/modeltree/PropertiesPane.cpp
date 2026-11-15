#include "ui/modeltree/PropertiesPane.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableView>
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
    expand_->setToolTip(tr("Widen the column to show the whole page"));
    expand_->hide();
    headerLayout->addWidget(title_, 1);
    headerLayout->addWidget(expand_);
    connect(expand_, &QToolButton::toggled, this, [this](bool expanded) {
        expand_->setToolTip(expanded ? tr("Narrow the column again")
                                     : tr("Widen the column to show the whole page"));
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
    // The stack holds only the page on screen: it is as wide and as tall as its tallest page,
    // hidden or not, so a page shown before would widen the column or leave it scrolling.
    if (auto* previous = pages_->currentWidget(); previous != nullptr && previous != page) {
        pages_->removeWidget(previous);
        previous->hide();
    }
    if (pages_->indexOf(page) < 0) pages_->addWidget(page);
    pages_->setCurrentWidget(page);
    page->show();
    // A table whose columns change, such as another card family's, can stop fitting or fit.
    for (const auto* header : page->findChildren<QHeaderView*>())
        connect(header, &QHeaderView::sectionResized, this, &PropertiesPane::updateExpandButton,
            Qt::UniqueConnection);
    title_->setText(title);
    show();
    updateExpandButton();
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

auto PropertiesPane::fullWidth() const -> int
{
    const auto* page = currentPage();
    if (page == nullptr) return 0;
    auto width = std::max(page->minimumSizeHint().width(), page->minimumWidth());
    const auto grids = page->findChildren<QTableView*>();
    const auto grid = std::ranges::find_if(grids,
        [page](const QTableView* table) { return table->isVisibleTo(page); });
    if (grid != grids.end()) {
        // Every column at the width its contents ask for; a stretched one gets its header's
        // width and room for a number.
        constexpr auto NumberWidth = 64;
        const auto* header = (*grid)->horizontalHeader();
        auto columns = 0;
        const auto last = header->count() - 1;
        for (auto column = 0; column <= last; ++column) {
            if (header->isSectionHidden(column)) continue;
            const auto stretched = header->sectionResizeMode(column) == QHeaderView::Stretch
                || (column == last && header->stretchLastSection());
            columns += stretched ? std::max(header->sectionSizeHint(column), NumberWidth)
                                 : header->sectionSize(column);
        }
        const auto rows = (*grid)->verticalHeader()->isVisible() ? (*grid)->verticalHeader()->width() : 0;
        const auto frame = 2 * (*grid)->frameWidth() + (*grid)->verticalScrollBar()->sizeHint().width();
        // The page's own margins around the table.
        const auto around = std::max(0, page->width() - (*grid)->width());
        width = std::max(width, columns + rows + frame + around);
    }
    // The pane's own scroll bar and margins.
    constexpr auto Slack = 24;
    return width + Slack;
}

void PropertiesPane::updateExpandButton()
{
    expand_->setVisible(isExpanded() || fullWidth() > width());
}

void PropertiesPane::setExpanded(bool expanded)
{
    const QSignalBlocker blocker(expand_);
    expand_->setChecked(expanded);
    updateExpandButton();
}

auto PropertiesPane::isExpanded() const -> bool
{
    return expand_->isChecked();
}

void PropertiesPane::setExpandCallback(std::function<void(bool)> callback)
{
    expanded_ = std::move(callback);
}

void PropertiesPane::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateExpandButton();
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
