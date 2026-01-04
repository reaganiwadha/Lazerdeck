#include "ScriptEditor.hpp"
#include <QAbstractItemView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QStringListModel>
#include <QApplication>
#include <QTimer>

// --- ConsoleEdit ---

ConsoleEdit::ConsoleEdit(QWidget *parent)
    : QPlainTextEdit(parent), c(nullptr) {
    setStyleSheet("QPlainTextEdit { background-color: #282a36; color: #f8f8f2; font-family: Consolas, 'Courier New', monospace; font-size: 14px; selection-background-color: #44475a; border: none; }");
}

void ConsoleEdit::setCompleter(QCompleter *completer) {
    if (c) c->disconnect(this);

    c = completer;
    if (!c) return;

    c->setWidget(this);
    c->setCompletionMode(QCompleter::PopupCompletion);
    c->setCaseSensitivity(Qt::CaseInsensitive);
    QObject::connect(c, QOverload<const QString &>::of(&QCompleter::activated),
                     this, &ConsoleEdit::insertCompletion);
}

QCompleter *ConsoleEdit::completer() const {
    return c;
}

void ConsoleEdit::insertCompletion(const QString &completion) {
    if (c->widget() != this) return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - c->completionPrefix().length();
    tc.movePosition(QTextCursor::Left);
    tc.movePosition(QTextCursor::EndOfWord);
    tc.insertText(completion.right(extra));
    setTextCursor(tc);
}

QString ConsoleEdit::textUnderCursor() const {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void ConsoleEdit::focusInEvent(QFocusEvent *e) {
    if (c) c->setWidget(this);
    QPlainTextEdit::focusInEvent(e);
}

void ConsoleEdit::keyPressEvent(QKeyEvent *e) {
    // Priority: Ctrl+Enter (Execute)
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) {
        if (c && c->popup()->isVisible()) {
             c->popup()->hide(); // Close popup if open
        }
        
        QTextCursor cursor = textCursor();
        cursor.select(QTextCursor::LineUnderCursor);
        QString cmd = cursor.selectedText();
        cmd = cmd.trimmed();
        
        if (!cmd.isEmpty()) {
            emit commandSubmitted(cmd);
            emit flashLineRequest();
        }
        e->accept(); // Consume event
        return;
    }

    if (c && c->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default: break;
        }
    }

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_E); 
    if (!c || !isShortcut) {
        // Check for Ctrl+Enter to send command
        // Removed from here since we moved it up
    }

    // Pass to base class
    QPlainTextEdit::keyPressEvent(e);

    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!c || (ctrlOrShift && e->text().isEmpty())) return;

    static QString eow = QString::fromStdString(R"(~!@#$%^&*()_+{}|:\"<>?,./;'[]\- =)"); 
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
    cr.setWidth(c->popup()->sizeHintForColumn(0) + c->popup()->verticalScrollBar()->sizeHint().width());
    c->complete(cr); 
}

// --- ScriptEditor (Container) ---

ScriptEditor::ScriptEditor(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Lazerdeck Console");
    resize(600, 300);
    setStyleSheet("background-color: #21222c;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    editor = new ConsoleEdit(this);
    highlighter = new LazerHighlighter(editor->document());

    // Status Label
    statusLabel = new QLabel("Ready | Ctrl+Enter to Run", this);
    statusLabel->setStyleSheet("color: #6272a4; background-color: #191a21; padding: 4px; font-family: Consolas; font-size: 12px;");

    layout->addWidget(editor);
    layout->addWidget(statusLabel);

    // Completer
    QCompleter *completer = new QCompleter(this);
    QStringList words;
    words << "play" << "pause" << "stop" << "load" << "s" << "restart";
    for(int i=1; i<=8; ++i) words << QString("$d%1").arg(i); // Update to $d1
    
    completer->setModel(new QStringListModel(words, completer));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setWrapAround(false);
    editor->setCompleter(completer);
    
    editor->setPlainText("# Lazerdeck Console\n# $d1 load \"file.mp3\"\n# $d1 play\n$d1 ");
    QTextCursor tc = editor->textCursor();
    tc.movePosition(QTextCursor::End);
    editor->setTextCursor(tc);

    connect(editor, &ConsoleEdit::commandSubmitted, this, &ScriptEditor::onCommandSubmitted);
    connect(editor, &ConsoleEdit::flashLineRequest, this, &ScriptEditor::flashCurrentLine);
}

void ScriptEditor::onCommandSubmitted(const QString &cmd) {
    statusLabel->setText("Last: " + cmd);
    statusLabel->setStyleSheet("color: #50fa7b; background-color: #191a21; padding: 4px; font-family: Consolas; font-size: 12px;");
    
    emit commandExecuted(cmd); // Forward signal

    // Reset status color after delay
    QTimer::singleShot(1000, [this]() {
         statusLabel->setStyleSheet("color: #6272a4; background-color: #191a21; padding: 4px; font-family: Consolas; font-size: 12px;");
    });
}

void ScriptEditor::flashCurrentLine() {
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor("#44475a"));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = editor->textCursor();
    selection.cursor.clearSelection();

    editor->setExtraSelections({selection});

    QTimer::singleShot(150, [this]() {
        editor->setExtraSelections({});
    });
}