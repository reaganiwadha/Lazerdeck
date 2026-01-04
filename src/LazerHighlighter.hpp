#pragma once

#include <QSyntaxHighlighter>
#include <QRegularExpression>

class LazerHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    LazerHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat deckFormat;
    QTextCharFormat keywordFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat numberFormat;
};
