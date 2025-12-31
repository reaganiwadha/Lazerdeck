#include "Engine.hpp"

int main(int argc, char *argv[]) {
    Engine engine;
    
    int numDecks = 4;
    if (argc > 1) {
        try {
            numDecks = std::stoi(argv[1]);
            if (numDecks < 1) numDecks = 1;
            if (numDecks > 8) numDecks = 8; // Reasonable limit
        } catch (...) {
            // Use default
        }
    }

    if (!engine.init(numDecks)) {
        return 1;
    }
    
    engine.run();
    
    return 0;
}