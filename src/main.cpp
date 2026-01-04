#include <QApplication>
#include <thread>
#include "Engine.hpp"
#include "ScriptEditor.hpp"
#include "Logger.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Parse args for numDecks
    int numDecks = 4;
    if (argc > 1) {
        try {
            numDecks = std::stoi(argv[1]);
            if (numDecks < 1) numDecks = 1;
            if (numDecks > 8) numDecks = 8;
        } catch (...) {}
    }

    Engine engine;
    
    // Create Editor
    ScriptEditor editor;
    editor.show();

    // Connect Editor to Engine
    QObject::connect(&editor, &ScriptEditor::commandExecuted, [&](const QString &cmd) {
        std::string command = cmd.toStdString();
        Logger::info("CMD: " + command);
        engine.pushCommand(command);
    });

    // Start Engine in a separate thread
    // Note: SDL Window creation happens in init(), so it must be called on the thread that runs the loop
    std::thread engineThread([&]() {
        if (engine.init(numDecks)) {
            engine.run();
        } else {
            Logger::error("Engine failed to initialize");
            // Optionally quit app?
        }
    });

    // Run Qt Event Loop
    int ret = app.exec();

    // Cleanup
    engine.stop();
    if (engineThread.joinable()) {
        engineThread.join();
    }

    return ret;
}
