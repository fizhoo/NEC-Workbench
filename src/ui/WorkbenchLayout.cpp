#include "ui/WorkbenchLayout.h"

#include "ui/BottomPanel.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace necwb::ui {
namespace {

const auto LeftColumnWidthKey = QStringLiteral("mainWindow/v2/leftColumnWidth");

// Never narrower than its content (which changes with density), so the column scrolls
// vertically but never clips sideways.
class LeftColumnScroll final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    [[nodiscard]] auto minimumSizeHint() const -> QSize override
    {
        return {widget()->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width(),
            QScrollArea::minimumSizeHint().height()};
    }
    [[nodiscard]] auto sizeHint() const -> QSize override
    {
        return {minimumSizeHint().width(), QScrollArea::sizeHint().height()};
    }
};

}

WorkbenchLayout::WorkbenchLayout(QWidget* welcome, QWidget* leftColumn, QWidget* center,
    BottomPanel* bottomPanel, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("workbenchLayout"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    pages_ = new QStackedWidget(this);

    auto* welcomePage = new QWidget(pages_);
    auto* welcomeLayout = new QHBoxLayout(welcomePage);
    welcomeLayout->setContentsMargins(0, 0, 0, 0);
    welcomeLayout->addWidget(welcome, 0, Qt::AlignHCenter | Qt::AlignTop);
    pages_->addWidget(welcomePage);

    // The left column keeps its content at the top and scrolls when the window is shorter.
    auto* leftScroll = new LeftColumnScroll;
    leftScroll->setObjectName(QStringLiteral("workbenchLeftColumn"));
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* leftContent = new QWidget(leftScroll);
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(leftColumn);
    leftLayout->addStretch();
    leftScroll->setWidget(leftContent);

    split_ = new QSplitter(Qt::Horizontal, pages_);
    split_->setObjectName(QStringLiteral("workbenchSplit"));
    split_->setHandleWidth(3);
    split_->setChildrenCollapsible(false);
    split_->addWidget(leftScroll);
    split_->addWidget(center);
    split_->setStretchFactor(0, 0);
    split_->setStretchFactor(1, 1);
    split_->setSizes({QSettings{}.value(LeftColumnWidthKey, leftScroll->sizeHint().width()).toInt(),
        QWIDGETSIZE_MAX});
    pages_->addWidget(split_);

    layout->addWidget(pages_, 1);
    layout->addWidget(bottomPanel);
    connect(split_, &QSplitter::splitterMoved, this, [this] { saveLeftColumnWidth(); });
}

void WorkbenchLayout::showWelcome()
{
    pages_->setCurrentIndex(0);
}

void WorkbenchLayout::showModel()
{
    pages_->setCurrentWidget(split_);
}

void WorkbenchLayout::resetLayout()
{
    split_->setSizes({split_->widget(0)->sizeHint().width(), QWIDGETSIZE_MAX});
    QSettings{}.remove(LeftColumnWidthKey);
}

void WorkbenchLayout::saveLeftColumnWidth() const
{
    QSettings{}.setValue(LeftColumnWidthKey, split_->sizes().value(0));
}

}
