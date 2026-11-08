#include "ui/bottom/RawOutputView.h"

#include "analysis/FrequencyComparison.h"
#include "ui/DisplayFormat.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace necwb::ui {

RawOutputView::RawOutputView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rawOutputView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(4, 0, 0, 0);
    controls->setSpacing(4);
    frequency_ = new QComboBox(this);
    frequency_->setObjectName(QStringLiteral("rawOutputFrequencyControl"));
    frequency_->setMinimumContentsLength(12);
    frequency_->setEnabled(false);
    frequency_->setToolTip(tr("Jump to the output section for this frequency"));
    auto* jump = new QToolButton(this);
    jump->setText(tr("Jump"));
    jump->setAutoRaise(true);
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("rawOutputFindControl"));
    find_->setPlaceholderText(tr("Find text"));
    find_->setClearButtonEnabled(true);
    auto* findNext = new QToolButton(this);
    findNext->setText(tr("Find Next"));
    findNext->setAutoRaise(true);
    controls->addWidget(frequency_);
    controls->addWidget(jump);
    controls->addWidget(find_, 1);
    controls->addWidget(findNext);
    output_ = new QPlainTextEdit(this);
    output_->setObjectName(QStringLiteral("analysisRawOutput"));
    output_->setReadOnly(true);
    output_->setFrameShape(QFrame::NoFrame);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setPlaceholderText(tr("Complete solver output will appear here."));
    output_->setToolTip(tr("Complete, immutable model.out from the latest loaded analysis. "
        "Parsed results do not alter this text."));
    layout->addLayout(controls);
    layout->addWidget(output_, 1);

    connect(jump, &QToolButton::clicked, this, [this] { jumpToSelectedFrequency(); });
    connect(findNext, &QToolButton::clicked, this, [this] { this->findNext(); });
    connect(find_, &QLineEdit::returnPressed, this, [this] { this->findNext(); });
}

void RawOutputView::setText(const QString& text)
{
    output_->setPlainText(text);
}

void RawOutputView::setFrequencies(const std::vector<double>& frequenciesMHz)
{
    frequency_->clear();
    for (const auto frequencyMHz : frequenciesMHz)
        frequency_->addItem(tr("%1 MHz").arg(formatDecimal(frequencyMHz)), frequencyMHz);
    frequency_->setEnabled(!frequenciesMHz.empty());
}

void RawOutputView::clear()
{
    output_->clear();
    setFrequencies({});
}

void RawOutputView::jumpToSelectedFrequency()
{
    if (frequency_->currentIndex() < 0 || output_->document()->isEmpty()) return;
    const auto target = frequency_->currentData().toDouble();
    static const QRegularExpression frequencyExpression(
        QStringLiteral("FREQUENCY\\s*:\\s*([+\\-0-9.EeDd]+)"),
        QRegularExpression::CaseInsensitiveOption);
    for (auto block = output_->document()->begin(); block.isValid(); block = block.next()) {
        const auto match = frequencyExpression.match(block.text());
        if (!match.hasMatch()) continue;
        auto valueText = match.captured(1);
        valueText.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool valid{};
        const auto value = valueText.toDouble(&valid);
        if (!valid || !nearlyEqual(value, target)) continue;
        output_->setTextCursor(QTextCursor(block));
        output_->centerCursor();
        emit statusMessage(
            tr("Raw output positioned at %1 MHz.").arg(formatDecimal(target)), 3000);
        return;
    }
    emit statusMessage(
        tr("No raw-output section was found for %1 MHz.").arg(formatDecimal(target)), 5000);
}

void RawOutputView::findNext()
{
    const auto text = find_->text();
    if (text.isEmpty() || output_->document()->isEmpty()) return;
    if (output_->find(text)) return;
    auto cursor = output_->textCursor();
    cursor.movePosition(QTextCursor::Start);
    output_->setTextCursor(cursor);
    if (!output_->find(text))
        emit statusMessage(tr("Text not found in raw output: %1").arg(text), 4000);
}

}
