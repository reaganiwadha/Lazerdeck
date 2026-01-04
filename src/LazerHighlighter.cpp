#include "LazerHighlighter.hpp"

LazerHighlighter::LazerHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Keywords
    keywordFormat.setForeground(QColor("#f1fa8c")); // Yellowish
    keywordFormat.setFontWeight(QFont::Bold);
    QStringList keywordPatterns;
    keywordPatterns << "\\bplay\\b" << "\\bpause\\b" << "\\bstop\\b" 
                    << "\\bload\\b" << "\\brestart\\b" << "\\bs\\b";
    for (const QString &pattern : keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // Decks ($d1, $d2...)
    deckFormat.setForeground(QColor("#8be9fd")); // Cyan
    deckFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("\\$d\\d+");
    rule.format = deckFormat;
    highlightingRules.append(rule);

    // Numbers
    numberFormat.setForeground(QColor("#bd93f9")); // Purple
    rule.pattern = QRegularExpression("\\b\\d+\\b");
    rule.format = numberFormat;
    highlightingRules.append(rule);

    // Comments
    commentFormat.setForeground(QColor("#6272a4")); // Greyish blue
    commentFormat.setFontItalic(true);
    rule.pattern = QRegularExpression("#[^\\n]*");
    rule.format = commentFormat;
    highlightingRules.append(rule);
}

void LazerHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
