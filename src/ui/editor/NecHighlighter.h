#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

namespace necwb::ui {

class NecHighlighter final : public QSyntaxHighlighter {
public:
    explicit NecHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat cardFormat_;
    QTextCharFormat commentFormat_;
    QTextCharFormat numberFormat_;
    QTextCharFormat unknownCardFormat_;
};

}
