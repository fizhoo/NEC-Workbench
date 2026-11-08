#include "ui/BottomPanel.h"

#include "ui/bottom/ProblemsView.h"
#include "ui/bottom/RunLogView.h"

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
constexpr std::array TabIds{"problems", "runLog"};

auto settingsKey(const char* name) -> QString
{
    return QStringLiteral("ui/bottomPanel/%1").arg(QLatin1String(name));
}

// Drag handle along the panel's bottom edge. The window grows downward from a fixed top, so
// the handle sits at the bottom where it follows the pointer.
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
            resize_(startHeight_ + event->globalPosition().toPoint().y() - startY_);
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
    pages_ = new QStackedWidget(this);
    problems_ = new ProblemsView(pages_);
    runLog_ = new RunLogView(pages_);
    for (auto* page : std::initializer_list<QWidget*>{problems_, runLog_})
        pages_->addWidget(page);
    resizeGrip_ = new ResizeGrip(this, [this] { return openHeight_; }, [this](int height) {
        openHeight_ = std::max(MinimumOpenHeight, height);
        saveState();
        emit heightChanged();
    });
    layout->addWidget(tabs_);
    layout->addWidget(pages_, 1);
    layout->addWidget(resizeGrip_);

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
    pages_->setVisible(!collapsed_);
    resizeGrip_->setVisible(!collapsed_);

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
    pages_->setVisible(!collapsed_);
    resizeGrip_->setVisible(!collapsed_);
    saveState();
    emit heightChanged();
}

void BottomPanel::resetLayout()
{
    openHeight_ = 220;
    setCollapsed(true);
    saveState();
    emit heightChanged();
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

auto BottomPanel::fitHeight(int availableHeight) -> int
{
    const auto stripHeight = tabs_->sizeHint().height();
    const auto height = collapsed_ ? stripHeight
        : stripHeight + std::clamp(availableHeight - stripHeight, MinimumOpenHeight, openHeight_);
    setFixedHeight(height);
    return height;
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
