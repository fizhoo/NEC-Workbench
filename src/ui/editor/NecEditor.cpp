#include "ui/editor/NecEditor.h"

#include <QFontDatabase>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>
#include <cmath>

namespace necwb::ui {

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(NecEditor* editor)
        : QWidget(editor)
        , editor_(editor)
    {
    }

    [[nodiscard]] auto sizeHint() const -> QSize override
    {
        return {editor_->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        editor_->lineNumberAreaPaintEvent(event);
    }

private:
    NecEditor* editor_;
};

NecEditor::NecEditor(QWidget* parent)
    : QPlainTextEdit(parent)
    , lineNumberArea_(new LineNumberArea(this))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
        [this](const QRect& rectangle, int verticalScroll) { updateLineNumberArea(rectangle, verticalScroll); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        lineNumberArea_->update();
        updateExtraSelections();
    });
    updateLineNumberAreaWidth();
    updateExtraSelections();
}

auto NecEditor::lineNumberAreaWidth() const -> int
{
    const auto digits = std::max(2, static_cast<int>(std::floor(std::log10(std::max(1, blockCount()))) + 1));
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void NecEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().alternateBase());

    auto block = firstVisibleBlock();
    auto blockNumber = block.blockNumber();
    auto top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    auto bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(blockNumber == textCursor().blockNumber()
                    ? palette().text().color()
                    : palette().placeholderText().color());
            painter.drawText(0, top, lineNumberArea_->width() - 5, fontMetrics().height(),
                Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void NecEditor::goToLine(std::size_t lineNumber)
{
    if (lineNumber == 0) {
        return;
    }
    const auto block = document()->findBlockByNumber(static_cast<int>(lineNumber - 1));
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

void NecEditor::setDiagnostics(std::span<const nec::ModelDiagnostic> diagnostics)
{
    diagnostics_.assign(diagnostics.begin(), diagnostics.end());
    updateExtraSelections();
}

void NecEditor::replaceTextAsSingleEdit(const QString& text)
{
    if (toPlainText() == text) return;
    auto cursor = QTextCursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
}

void NecEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const auto contents = contentsRect();
    lineNumberArea_->setGeometry({contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()});
}

void NecEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void NecEditor::updateLineNumberArea(const QRect& rectangle, int verticalScroll)
{
    if (verticalScroll != 0) {
        lineNumberArea_->scroll(0, verticalScroll);
    } else {
        lineNumberArea_->update(0, rectangle.y(), lineNumberArea_->width(), rectangle.height());
    }
    if (rectangle.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void NecEditor::updateExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection currentLine;
        currentLine.format.setBackground(palette().alternateBase());
        currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
        currentLine.cursor = textCursor();
        currentLine.cursor.clearSelection();
        selections.append(currentLine);
    }

    for (const auto& diagnostic : diagnostics_) {
        const auto block = document()->findBlockByNumber(static_cast<int>(diagnostic.lineNumber - 1));
        if (!block.isValid()) {
            continue;
        }
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(diagnostic.severity == nec::DiagnosticSeverity::Error
                ? QColor(255, 215, 215)
                : QColor(255, 244, 206));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(block);
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
}

}
