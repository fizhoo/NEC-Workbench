#include "ui/WorkbenchLayout.h"

#include "ui/BottomPanel.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace necwb::ui {
namespace {

const auto LeftColumnWidthKey = QStringLiteral("mainWindow/v2/leftColumnWidth");
const auto RightColumnWidthKey = QStringLiteral("mainWindow/v2/rightColumnWidth");
const auto RightColumnCollapsedKey = QStringLiteral("mainWindow/v2/rightColumnCollapsed");
constexpr auto LeftPane = 0;
constexpr auto RightPane = 2;

}

WorkbenchLayout::WorkbenchLayout(Panes panes, QWidget* parent)
    : QWidget(parent)
    , geometry_(panes.geometry)
    , fullView_(panes.fullView)
    , deck_(panes.deck)
    , bottomPanel_(panes.bottomPanel)
{
    setObjectName(QStringLiteral("workbenchLayout"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    pages_ = new QStackedWidget(this);

    auto* welcomePage = new QWidget(pages_);
    auto* welcomeLayout = new QHBoxLayout(welcomePage);
    welcomeLayout->setContentsMargins(0, 0, 0, 0);
    welcomeLayout->addWidget(panes.welcome, 0, Qt::AlignHCenter | Qt::AlignTop);
    pages_->addWidget(welcomePage);

    center_ = new QStackedWidget;
    center_->setObjectName(QStringLiteral("workbenchCenter"));
    center_->addWidget(geometry_);
    center_->addWidget(fullView_);
    center_->addWidget(deck_);

    split_ = new QSplitter(Qt::Horizontal, pages_);
    split_->setObjectName(QStringLiteral("workbenchSplit"));
    split_->setHandleWidth(3);
    split_->addWidget(panes.leftColumn);
    split_->addWidget(center_);
    split_->addWidget(panes.rightColumn);
    split_->setCollapsible(LeftPane, false);
    split_->setCollapsible(1, false);
    split_->setCollapsible(RightPane, true);
    split_->setStretchFactor(LeftPane, 0);
    split_->setStretchFactor(1, 1);
    split_->setStretchFactor(RightPane, 0);
    split_->installEventFilter(this);
    pages_->addWidget(split_);

    layout->addWidget(pages_, 1);
    layout->addWidget(bottomPanel_);

    rightColumnAction_ = new QAction(tr("&Results Column"), this);
    rightColumnAction_->setObjectName(QStringLiteral("toggleResultsColumnAction"));
    rightColumnAction_->setCheckable(true);
    rightColumnAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    rightColumnAction_->setChecked(!QSettings{}.value(RightColumnCollapsedKey, false).toBool());
    connect(rightColumnAction_, &QAction::toggled, this,
        [this](bool shown) { setRightColumnShown(shown); });
    connect(split_, &QSplitter::splitterMoved, this, [this] { saveColumnWidths(); });
}

void WorkbenchLayout::showWelcome()
{
    pages_->setCurrentIndex(0);
}

void WorkbenchLayout::showModel()
{
    pages_->setCurrentWidget(split_);
}

void WorkbenchLayout::showGeometry()
{
    center_->setCurrentWidget(geometry_);
}

void WorkbenchLayout::showFullView()
{
    center_->setCurrentWidget(fullView_);
}

void WorkbenchLayout::showDeck()
{
    center_->setCurrentWidget(deck_);
}

auto WorkbenchLayout::fullViewShown() const -> bool
{
    return center_->currentWidget() == fullView_;
}

void WorkbenchLayout::resetLayout()
{
    QSettings settings;
    settings.remove(LeftColumnWidthKey);
    settings.remove(RightColumnWidthKey);
    settings.remove(RightColumnCollapsedKey);
    rightColumnAction_->setChecked(true);
    applyColumnWidths(split_->widget(LeftPane)->sizeHint().width(),
        split_->widget(RightPane)->sizeHint().width());
}

auto WorkbenchLayout::minimumSizeHint() const -> QSize
{
    // Leave room for the open bottom panel even while it is collapsed, so opening it never
    // has to grow the window.
    auto hint = QWidget::minimumSizeHint();
    hint.setHeight(std::max(hint.height(),
        pages_->minimumSizeHint().height() + bottomPanel_->openMinimumHeight()));
    return hint;
}

auto WorkbenchLayout::eventFilter(QObject* watched, QEvent* event) -> bool
{
    if (watched == split_ && event->type() == QEvent::Resize && !columnWidthsApplied_
        && split_->width() > 0) {
        columnWidthsApplied_ = true;
        const QSettings settings;
        applyColumnWidths(
            settings.value(LeftColumnWidthKey, split_->widget(LeftPane)->sizeHint().width()).toInt(),
            rightColumnAction_->isChecked() ? settings.value(RightColumnWidthKey,
                split_->widget(RightPane)->sizeHint().width()).toInt() : 0);
    }
    return QWidget::eventFilter(watched, event);
}

void WorkbenchLayout::applyColumnWidths(int left, int right)
{
    const auto* leftColumn = split_->widget(LeftPane);
    left = std::clamp(left, leftColumn->minimumSizeHint().width(), leftColumn->maximumWidth());
    if (right > 0) right = std::max(right, split_->widget(RightPane)->minimumSizeHint().width());
    const auto center = split_->width() - 2 * split_->handleWidth() - left - right;
    split_->setSizes({left, std::max(0, center), right});
}

void WorkbenchLayout::saveColumnWidths()
{
    const auto sizes = split_->sizes();
    QSettings settings;
    settings.setValue(LeftColumnWidthKey, sizes.value(LeftPane));
    // Dragging the right column shut collapses it; its width is kept for when it comes back.
    const auto right = sizes.value(RightPane);
    if (right > 0) settings.setValue(RightColumnWidthKey, right);
    const QSignalBlocker blocker(rightColumnAction_);
    rightColumnAction_->setChecked(right > 0);
    settings.setValue(RightColumnCollapsedKey, right == 0);
}

void WorkbenchLayout::setRightColumnShown(bool shown)
{
    QSettings settings;
    settings.setValue(RightColumnCollapsedKey, !shown);
    applyColumnWidths(split_->sizes().value(LeftPane), shown
        ? settings.value(RightColumnWidthKey, split_->widget(RightPane)->sizeHint().width()).toInt()
        : 0);
}

}
