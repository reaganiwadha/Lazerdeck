#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include "Deck.hpp"
#include "Logger.hpp"

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init();
    void clear();
    void present();
    
    void renderDeck(Deck* deck, int yOffset, int height, int samplesPerPixel, const char* name, bool isActive);
    void drawLogs();
    
    void setWindowTitle(const std::string& title);
    int getRefreshRate();

    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    void drawText(int x, int y, const std::string& text, SDL_Color color, int fontSize = 24);
    void drawTime(int x, int y, int seconds);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_Font* logFont = nullptr;
    
    int width = 1280;
    int height = 720;
};
