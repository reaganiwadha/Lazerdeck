#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QLabel>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
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

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void commandExecuted(const QString &cmd);
    void appQuitRequested();

private slots:
    void onCommandSubmitted(const QString &cmd);
    void flashCurrentLine();
    void onTextChanged();
    
    // Actions
    void newFile();
    void openFile();
    void saveFile();
    void saveFileAs();
    void openDemo();

private:
    bool maybeSave();
    void loadFile(const QString &fileName);
    bool saveFile(const QString &fileName);
    void setCurrentFile(const QString &fileName);

    ConsoleEdit *editor;
    QLabel *statusLabel;
    LazerHighlighter *highlighter;
    
    QString currentFile;
    bool isModified;
};
