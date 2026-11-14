#include "ui/CenterViewHeader.h"

#include "ui/ElidedLabel.h"

#include <QHBoxLayout>
#include <QShortcut>
#include <QToolButton>

namespace necwb::ui {

CenterViewHeader::CenterViewHeader(const QString& objectName, QWidget* view) : QWidget(view)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 6, 2);
    auto* back = new QToolButton(this);
    back->setObjectName(objectName + QStringLiteral("Back"));
    back->setText(tr("← Geometry"));
    back->setToolTip(tr("Return to the geometry editor (Esc)"));
    back->setAutoRaise(true);
    title_ = new ElidedLabel(this);
    title_->setObjectName(objectName + QStringLiteral("Title"));
    auto font = title_->font();
    font.setBold(true);
    title_->setFont(font);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(back);
    layout->addWidget(title_, 1);
    connect(back, &QToolButton::clicked, this, [this] { if (back_) back_(); });
    // Only while the view has focus: elsewhere in the window Esc belongs to the properties
    // pane, where it reverts an edit.
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), view);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { if (back_) back_(); });
}

void CenterViewHeader::setTitle(const QString& title)
{
    title_->setText(title);
}

void CenterViewHeader::addTrailingWidget(QWidget* widget)
{
    layout()->addWidget(widget);
}

void CenterViewHeader::setBackCallback(std::function<void()> callback)
{
    back_ = std::move(callback);
}

}
