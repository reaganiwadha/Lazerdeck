#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QLabel>
#include <QVBoxLayout>
#include "LazerHighlighter.hpp"

class ConsoleEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ConsoleEdit(QWidget *parent = nullptr);
    void setCompleter(QCompleter *c);
    QCompleter *completer() const;

signals:
    void commandSubmitted(const QString &cmd);
    void flashLineRequest();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private slots:
    void insertCompletion(const QString &completion);

private:
    QString textUnderCursor() const;
    QCompleter *c;
};

class ScriptEditor : public QWidget {
    Q_OBJECT

public:
    explicit ScriptEditor(QWidget *parent = nullptr);

signals:
    void commandExecuted(const QString &cmd);

private slots:
    void onCommandSubmitted(const QString &cmd);
    void flashCurrentLine();

private:
    ConsoleEdit *editor;
    QLabel *statusLabel;
    LazerHighlighter *highlighter;
};