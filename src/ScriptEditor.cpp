#include "ScriptEditor.hpp"
#include <QAbstractItemView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QStringListModel>
#include <QApplication>

ScriptEditor::ScriptEditor(QWidget *parent)
    : QPlainTextEdit(parent), c(nullptr) {
    setWindowTitle("Lazerdeck Console");
    resize(600, 400);
    setStyleSheet("QPlainTextEdit { background-color: #2b2b2b; color: #f0f0f0; font-family: Consolas, 'Courier New', monospace; font-size: 14px; selection-background-color: #4a90e2; }");
    
    setPlainText("Hello World\n");
    
    // Setup default completer
    QCompleter *completer = new QCompleter(this);
    QStringList words;
    words << "play" << "pause" << "stop" << "deck" << "bpm" << "load" << "sync" << "seek" << "volume" << "speed";
    completer->setModel(new QStringListModel(words, completer));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setWrapAround(false);
    setCompleter(completer);
}

void ScriptEditor::setCompleter(QCompleter *completer) {
    if (c)
        c->disconnect(this);

    c = completer;

    if (!c)
        return;

    c->setWidget(this);
    c->setCompletionMode(QCompleter::PopupCompletion);
    c->setCaseSensitivity(Qt::CaseInsensitive);
    QObject::connect(c, QOverload<const QString &>::of(&QCompleter::activated),
                     this, &ScriptEditor::insertCompletion);
}

QCompleter *ScriptEditor::completer() const {
    return c;
}

void ScriptEditor::insertCompletion(const QString &completion) {
    if (c->widget() != this)
        return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - c->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

QString ScriptEditor::textUnderCursor() const {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void ScriptEditor::focusInEvent(QFocusEvent *e) {
    if (c)
        c->setWidget(this);
    QPlainTextEdit::focusInEvent(e);
}

void ScriptEditor::keyPressEvent(QKeyEvent *e) {
    if (c && c->popup()->isVisible()) {
        // The following keys are forwarded by the completer to the widget
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return; // let the completer do default behavior
        default:
            break;
        }
    }

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_E); // Ctrl+E
    if (!c || !isShortcut) // do not process the shortcut when we have a completer
        // QPlainTextEdit::keyPressEvent(e); // Don't call this yet, check for Enter first

    // Check for Enter to send command
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (e->modifiers() & Qt::ShiftModifier) {
            // Shift+Enter -> New line
            QPlainTextEdit::keyPressEvent(e);
            return;
        } else if (!c || !c->popup()->isVisible()) {
            // Enter -> Send command (if popup not visible)
            QTextCursor cursor = textCursor();
            cursor.select(QTextCursor::LineUnderCursor);
            QString cmd = cursor.selectedText();
            emit commandExecuted(cmd);
            
            // Optional: Move to next line or clear?
            // For a console, maybe just insert a newline after execution?
            QPlainTextEdit::keyPressEvent(e); 
            return;
        }
    }

    // Pass to base class for normal typing
    QPlainTextEdit::keyPressEvent(e);

    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!c || (ctrlOrShift && e->text().isEmpty()))
        return;

    static QString eow = QString::fromStdString(R"(~!@#$%^&*()_+{}|:"<>?,./;'[]\- =)"); // end of word
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;
    QString completionPrefix = textUnderCursor();

    if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 1
                      || eow.contains(e->text().right(1)))) {
        c->popup()->hide();
        return;
    }

    if (completionPrefix != c->completionPrefix()) {
        c->setCompletionPrefix(completionPrefix);
        c->popup()->setCurrentIndex(c->completionModel()->index(0, 0));
    }
    QRect cr = cursorRect();
    cr.setWidth(c->popup()->sizeHintForColumn(0)
                + c->popup()->verticalScrollBar()->sizeHint().width());
    c->complete(cr); // popup it up!
}
