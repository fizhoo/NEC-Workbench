#include "ui/BottomPanel.h"

#include "ui/bottom/ProblemsView.h"
#include "ui/bottom/RawOutputView.h"
#include "ui/bottom/RunHistoryView.h"
#include "ui/bottom/RunLogView.h"

#include <QAction>
#include <QMouseEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace necwb::ui {
namespace {

constexpr auto MinimumOpenHeight = 80;
constexpr auto GripHeight = 4;
constexpr std::array TabIds{"problems", "runLog", "runs", "rawOutput"};

auto settingsKey(const char* name) -> QString
{
    return QStringLiteral("ui/bottomPanel/%1").arg(QLatin1String(name));
}

// Drag handle along the panel's top edge; dragging up makes the panel taller.
class ResizeGrip final : public QWidget {
public:
    ResizeGrip(QWidget* parent, std::function<int()> height, std::function<void(int)> resize)
        : QWidget(parent), height_(std::move(height)), resize_(std::move(resize))
    {
        setFixedHeight(GripHeight);
        setCursor(Qt::SizeVerCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        startY_ = event->globalPosition().toPoint().y();
        startHeight_ = height_();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton)
            resize_(startHeight_ - (event->globalPosition().toPoint().y() - startY_));
    }

private:
    std::function<int()> height_;
    std::function<void(int)> resize_;
    int startY_{};
    int startHeight_{};
};

}

BottomPanel::BottomPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bottomPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("bottomPanelTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setDrawBase(true);
    tabs_->setExpanding(false);
    tabs_->addTab(tr("Problems"));
    tabs_->addTab(tr("Run log"));
    tabs_->addTab(tr("Runs"));
    tabs_->addTab(tr("Raw output"));
    pages_ = new QStackedWidget(this);
    problems_ = new ProblemsView(pages_);
    runLog_ = new RunLogView(pages_);
    runs_ = new RunHistoryView(pages_);
    rawOutput_ = new RawOutputView(pages_);
    for (auto* page : std::initializer_list<QWidget*>{problems_, runLog_, runs_, rawOutput_})
        pages_->addWidget(page);
    resizeGrip_ = new ResizeGrip(this, [this] { return pages_->height(); }, [this](int height) {
        // Leave the area above at least as tall as the panel's minimum.
        const auto limit = parentWidget() == nullptr ? height
            : parentWidget()->height() - tabs_->sizeHint().height() - GripHeight - MinimumOpenHeight;
        openHeight_ = std::max(MinimumOpenHeight, std::min(height, limit));
        applyHeight();
        saveState();
    });
    layout->addWidget(resizeGrip_);
    layout->addWidget(tabs_);
    layout->addWidget(pages_, 1);

    const QSettings settings;
    const auto savedTab = settings.value(settingsKey("currentTab")).toString();
    const auto found = std::ranges::find_if(TabIds,
        [&savedTab](const char* id) { return savedTab == QLatin1String(id); });
    const auto index = found == TabIds.end() ? 0 : static_cast<int>(found - TabIds.begin());
    tabs_->setCurrentIndex(index);
    pages_->setCurrentIndex(index);
    openHeight_ = std::max(MinimumOpenHeight,
        settings.value(settingsKey("openHeight"), openHeight_).toInt());
    collapsed_ = settings.value(settingsKey("collapsed"), true).toBool();
    applyHeight();
    toggleAction_ = new QAction(tr("&Bottom Panel"), this);
    toggleAction_->setObjectName(QStringLiteral("toggleBottomPanelAction"));
    toggleAction_->setCheckable(true);
    toggleAction_->setChecked(!collapsed_);
    toggleAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    toggleAction_->setStatusTip(tr("Open or collapse the bottom panel"));
    connect(toggleAction_, &QAction::toggled, this, [this](bool open) { setCollapsed(!open); });

    // tabBarClicked arrives before the bar changes its current tab, so a click on the
    // already-current tab is the collapse/expand toggle.
    connect(tabs_, &QTabBar::tabBarClicked, this, [this](int clicked) {
        if (clicked < 0) return;
        if (clicked == tabs_->currentIndex()) setCollapsed(!collapsed_);
        else showTab(static_cast<Tab>(clicked));
    });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int current) {
        pages_->setCurrentIndex(current);
        saveState();
    });
}

auto BottomPanel::currentTab() const -> Tab
{
    return static_cast<Tab>(tabs_->currentIndex());
}

void BottomPanel::showTab(Tab tab)
{
    tabs_->setCurrentIndex(static_cast<int>(tab));
    setCollapsed(false);
}

void BottomPanel::setCollapsed(bool collapsed)
{
    if (collapsed_ == collapsed) return;
    collapsed_ = collapsed;
    applyHeight();
    toggleAction_->setChecked(!collapsed_);
    saveState();
}

void BottomPanel::resetLayout()
{
    openHeight_ = 220;
    setCollapsed(true);
    applyHeight();
    saveState();
}

void BottomPanel::openForRun()
{
    collapsedBeforeRun_ = collapsed_;
    showTab(Tab::RunLog);
}

void BottomPanel::finishRun(bool succeeded)
{
    if (succeeded && collapsedBeforeRun_ && currentTab() == Tab::RunLog) setCollapsed(true);
    collapsedBeforeRun_ = false;
}

auto BottomPanel::sizeHint() const -> QSize
{
    const auto stripHeight = tabs_->sizeHint().height();
    return {QWidget::sizeHint().width(),
        collapsed_ ? stripHeight : GripHeight + stripHeight + openHeight_};
}

auto BottomPanel::openMinimumHeight() const -> int
{
    return GripHeight + tabs_->sizeHint().height() + MinimumOpenHeight;
}

void BottomPanel::applyHeight()
{
    // Open, the panel takes its remembered height but gives way when the window is short.
    // The policy changes before the pages show, since showing them lays the window out.
    setSizePolicy(QSizePolicy::Preferred, collapsed_ ? QSizePolicy::Fixed : QSizePolicy::Maximum);
    setMinimumHeight(collapsed_ ? 0 : openMinimumHeight());
    pages_->setVisible(!collapsed_);
    resizeGrip_->setVisible(!collapsed_);
    updateGeometry();
}

void BottomPanel::saveState() const
{
    QSettings settings;
    settings.setValue(settingsKey("currentTab"),
        QLatin1String(TabIds[static_cast<std::size_t>(tabs_->currentIndex())]));
    settings.setValue(settingsKey("openHeight"), openHeight_);
    settings.setValue(settingsKey("collapsed"), collapsed_);
}

}
