#include "ui/editor/NecHighlighter.h"

#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>

namespace necwb::ui {

NecHighlighter::NecHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document)
{
    cardFormat_.setForeground(QColor(31, 78, 121));
    cardFormat_.setFontWeight(QFont::Bold);
    commentFormat_.setForeground(QColor(72, 118, 72));
    numberFormat_.setForeground(QColor(128, 48, 128));
    unknownCardFormat_.setForeground(QColor(178, 34, 34));
    unknownCardFormat_.setFontWeight(QFont::Bold);
}

void NecHighlighter::highlightBlock(const QString& text)
{
    static const QRegularExpression cardExpression(QStringLiteral("^\\s*([A-Za-z]{2})\\b"));
    static const QRegularExpression numberExpression(
        QStringLiteral("(?<![A-Za-z_])[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?"));
    static const QSet<QString> knownCards{
        QStringLiteral("CM"), QStringLiteral("CE"), QStringLiteral("GW"), QStringLiteral("GE"),
        QStringLiteral("EX"), QStringLiteral("LD"), QStringLiteral("GN"), QStringLiteral("FR"),
        QStringLiteral("RP"), QStringLiteral("TL"), QStringLiteral("NT"), QStringLiteral("EN")};

    const auto cardMatch = cardExpression.match(text);
    if (!cardMatch.hasMatch()) {
        return;
    }

    const auto mnemonic = cardMatch.captured(1).toUpper();
    if (mnemonic == QStringLiteral("CM") || mnemonic == QStringLiteral("CE")) {
        setFormat(0, text.size(), commentFormat_);
        return;
    }

    setFormat(cardMatch.capturedStart(1), cardMatch.capturedLength(1),
        knownCards.contains(mnemonic) ? cardFormat_ : unknownCardFormat_);

    auto numberMatch = numberExpression.globalMatch(text, cardMatch.capturedEnd(1));
    while (numberMatch.hasNext()) {
        const auto match = numberMatch.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat_);
    }
}

}
