#pragma once

#include "nec/NecModelChecker.h"

#include <QPlainTextEdit>

#include <span>
#include <vector>

class QPaintEvent;
class QResizeEvent;

namespace necwb::ui {

class LineNumberArea;

class NecEditor final : public QPlainTextEdit {
public:
    explicit NecEditor(QWidget* parent = nullptr);

    [[nodiscard]] auto lineNumberAreaWidth() const -> int;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void goToLine(std::size_t lineNumber);
    void setDiagnostics(std::span<const nec::ModelDiagnostic> diagnostics);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rectangle, int verticalScroll);
    void updateExtraSelections();

    LineNumberArea* lineNumberArea_{};
    std::vector<nec::ModelDiagnostic> diagnostics_;
};

}
