#include "ui/editor/DeckView.h"

#include "ui/CenterViewHeader.h"

#include <QVBoxLayout>

namespace necwb::ui {

DeckView::DeckView(QWidget* deck, QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("necDeckView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new CenterViewHeader(QStringLiteral("necDeckView"), this);
    header_->setTitle(tr("NEC Deck"));
    layout->addWidget(header_);
    layout->addWidget(deck, 1);
}

void DeckView::setBackCallback(std::function<void()> callback)
{
    header_->setBackCallback(std::move(callback));
}

}
