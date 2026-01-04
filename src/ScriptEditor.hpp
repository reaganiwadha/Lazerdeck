#pragma once

#include <QPlainTextEdit>
#include <QCompleter>
#include <QWidget>

class ScriptEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ScriptEditor(QWidget *parent = nullptr);
    void setCompleter(QCompleter *c);
    QCompleter *completer() const;

signals:
    void commandExecuted(const QString &cmd);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private slots:
    void insertCompletion(const QString &completion);

private:
    QString textUnderCursor() const;
    QCompleter *c;
};
