#include "ScriptEditor.hpp"
#include <QAbstractItemView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QStringListModel>
#include <QApplication>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFileInfo>

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
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) {
        if (c && c->popup()->isVisible()) {
             c->popup()->hide(); 
        }
        
        QTextCursor cursor = textCursor();
        cursor.select(QTextCursor::LineUnderCursor);
        QString cmd = cursor.selectedText();
        cmd = cmd.trimmed();
        
        if (!cmd.isEmpty()) {
            emit commandSubmitted(cmd);
            emit flashLineRequest();
        }
        e->accept();
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

    QPlainTextEdit::keyPressEvent(e);

    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!c || (ctrlOrShift && e->text().isEmpty())) return;

    static QString eow = QString::fromStdString(R"(~!@#$%^&*()_+{}|:\"<>?,./;'[]\- =)"); 
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;
    QString completionPrefix = textUnderCursor();

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_E); 

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

ScriptEditor::ScriptEditor(QWidget *parent) : QWidget(parent), isModified(false) {
    setWindowTitle("Lazerdeck Console - Untitled");
    resize(600, 400);
    setStyleSheet("background-color: #21222c;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Menu Bar
    QMenuBar *menuBar = new QMenuBar(this);
    menuBar->setStyleSheet("QMenuBar { background-color: #191a21; color: #f8f8f2; } QMenuBar::item:selected { background-color: #44475a; } QMenu { background-color: #282a36; color: #f8f8f2; border: 1px solid #44475a; } QMenu::item:selected { background-color: #44475a; }");
    
    QMenu *fileMenu = menuBar->addMenu("&File");
    
    QAction *newAct = fileMenu->addAction("&New");
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &ScriptEditor::newFile);
    
    QAction *openAct = fileMenu->addAction("&Open...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &ScriptEditor::openFile);
    
    QAction *saveAct = fileMenu->addAction("&Save");
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this]() { saveFile(); });

    QAction *saveAsAct = fileMenu->addAction("Save &As...");
    connect(saveAsAct, &QAction::triggered, this, &ScriptEditor::saveFileAs);
    
    fileMenu->addSeparator();

    QAction *demoAct = fileMenu->addAction("Open &Demo Script");
    connect(demoAct, &QAction::triggered, this, &ScriptEditor::openDemo);
    
    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction("E&xit");
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar->addMenu("&Help");
    QAction *helpAct = helpMenu->addAction("&Command Reference");
    helpAct->setShortcut(QKeySequence::HelpContents);
    connect(helpAct, &QAction::triggered, this, &ScriptEditor::showHelp);

    layout->setMenuBar(menuBar);

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
    words << "play" << "pause" << "stop" << "load" << "speed" << "seek" << "volume" << "sync" << "loop" << "loop_exit" << "cue" << "goto_cue" << "s" << "restart";
    for(int i=1; i<=8; ++i) words << QString("$d%1").arg(i); 
    
    completer->setModel(new QStringListModel(words, completer));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setWrapAround(false);
    editor->setCompleter(completer);
    
    connect(editor, &ConsoleEdit::commandSubmitted, this, &ScriptEditor::onCommandSubmitted);
    connect(editor, &ConsoleEdit::flashLineRequest, this, &ScriptEditor::flashCurrentLine);
    connect(editor->document(), &QTextDocument::contentsChanged, this, &ScriptEditor::onTextChanged);

    setCurrentFile("");
}

void ScriptEditor::onCommandSubmitted(const QString &cmd) {
    statusLabel->setText("Last: " + cmd);
    statusLabel->setStyleSheet("color: #50fa7b; background-color: #191a21; padding: 4px; font-family: Consolas; font-size: 12px;");
    
    emit commandExecuted(cmd);

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

void ScriptEditor::onTextChanged() {
    if (!isModified) {
        isModified = true;
        setWindowModified(true);
        setWindowTitle("Lazerdeck Console - " + (currentFile.isEmpty() ? "Untitled" : QFileInfo(currentFile).fileName()) + "*");
    }
}

void ScriptEditor::closeEvent(QCloseEvent *event) {
    if (maybeSave()) {
        emit appQuitRequested();
        event->accept();
    } else {
        event->ignore();
    }
}

void ScriptEditor::newFile() {
    if (maybeSave()) {
        editor->clear();
        setCurrentFile("");
    }
}

void ScriptEditor::openFile() {
    if (maybeSave()) {
        QString fileName = QFileDialog::getOpenFileName(this, "Open Script", "", "Lazer Scripts (*.lazerscript *.txt);;All Files (*)");
        if (!fileName.isEmpty())
            loadFile(fileName);
    }
}

void ScriptEditor::saveFile() {
    if (currentFile.isEmpty()) {
        saveFileAs();
    } else {
        saveFile(currentFile);
    }
}

void ScriptEditor::saveFileAs() {
    QString fileName = QFileDialog::getSaveFileName(this, "Save Script", "", "Lazer Scripts (*.lazerscript);;Text Files (*.txt);;All Files (*)");
    if (!fileName.isEmpty())
        saveFile(fileName);
}

void ScriptEditor::openDemo() {
    if (maybeSave()) {
        editor->setPlainText(
            "# Demo Script\n"
            "$d1 load \"resources/znfodastica.wav\"\n"
            "$d2 load \"resources/glory.mp3\"\n"
            "$d1 play\n"
            "$d2 play\n"
        );
        setCurrentFile("");
        isModified = true;
        setWindowModified(true);
        setWindowTitle("Lazerdeck Console - Untitled*");
    }
}

void ScriptEditor::showHelp() {
    QMessageBox::information(this, "Lazerdeck Command Reference",
        "<h3>Available Commands</h3>"
        "<p>Commands start with <b>$dX</b> where X is the deck number (1-8).</p>"
        "<ul>"
        "<li><b>$dX play</b> - Start playback</li>"
        "<li><b>$dX pause</b> - Pause playback</li>"
        "<li><b>$dX stop</b> - Stop and rewind to start</li>"
        "<li><b>$dX load \"path/to/file\"</b> - Load an audio file</li>"
        "<li><b>$dX speed &lt;factor&gt;</b> - Set playback speed (e.g. 1.0, 1.2, 0.8)</li>"
        "<li><b>$dX seek &lt;seconds&gt;</b> - Jump relative in seconds</li>"
        "<li><b>$dX volume &lt;level&gt;</b> - Set volume (0.0 - 1.0)</li>"
        "<li><b>$dX sync &lt;deck&gt;</b> - Sync to another deck (e.g. $d1 sync 2)</li>"
        "<li><b>$dX loop &lt;start&gt; &lt;end&gt;</b> - Set loop in beats</li>"
        "<li><b>$dX loop_exit</b> - Exit loop</li>"
        "<li><b>$dX cue &lt;id&gt;</b> - Set hot cue at current position</li>"
        "<li><b>$dX goto_cue &lt;id&gt;</b> - Jump to hot cue</li>"
        "</ul>"
        "<p><b>Shortcuts:</b></p>"
        "<ul>"
        "<li><b>Ctrl+Enter</b> - Run current line</li>"
        "<li><b>Ctrl+Space</b> - Auto-complete</li>"
        "</ul>"
    );
}

bool ScriptEditor::maybeSave() {
    if (!isModified)
        return true;
    
    const QMessageBox::StandardButton ret = QMessageBox::warning(this, "Lazerdeck Console",
                               "The document has been modified.\n"
                               "Do you want to save your changes?",
                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (ret) {
    case QMessageBox::Save:
        saveFile();
        return !isModified; 
    case QMessageBox::Cancel:
        return false;
    default:
        break;
    }
    return true;
}

void ScriptEditor::loadFile(const QString &fileName) {
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, "Lazerdeck Console", "Cannot read file " + fileName + ":\n" + file.errorString());
        return;
    }

    QTextStream in(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    editor->setPlainText(in.readAll());
    QApplication::restoreOverrideCursor();

    setCurrentFile(fileName);
}

bool ScriptEditor::saveFile(const QString &fileName) {
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, "Lazerdeck Console", "Cannot write file " + fileName + ":\n" + file.errorString());
        return false;
    }

    QTextStream out(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    out << editor->toPlainText();
    QApplication::restoreOverrideCursor();

    setCurrentFile(fileName);
    return true;
}

void ScriptEditor::setCurrentFile(const QString &fileName) {
    currentFile = fileName;
    isModified = false;
    setWindowModified(false);
    
    QString shownName = currentFile;
    if (currentFile.isEmpty())
        shownName = "Untitled";
    else
        shownName = QFileInfo(currentFile).fileName();
    
    setWindowTitle("Lazerdeck Console - " + shownName);
}
