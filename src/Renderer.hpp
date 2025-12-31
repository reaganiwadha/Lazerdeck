#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <chrono>
#include <unordered_map>
#include <vector>
#include "Deck.hpp"
#include "Logger.hpp"

struct RenderPass {
    const char* name;
    bool enabled = true;
    double lastTimeMs = 0.0;
};

struct CachedTexture {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    uint64_t lastUsed = 0;
};

class TextCache {
public:
    TextCache(SDL_Renderer* renderer, TTF_Font* font);
    ~TextCache();
    
    SDL_Texture* get(const std::string& text, SDL_Color color, int fontSize = 24);
    void getTextureSize(SDL_Texture* texture, int* w, int* h);
    void cleanupOldTextures(uint64_t maxAgeFrames = 600); // Clean up textures not used for ~10 seconds at 60fps
    
private:
    SDL_Renderer* renderer;
    TTF_Font* font;
    std::unordered_map<std::string, CachedTexture> cache;
    uint64_t frameCounter = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init();
    void clear();
    void present();
    
    void renderDeck(Deck* deck, int yOffset, int height, int samplesPerPixel, const char* name, bool isActive);
    void drawLogs();
    void drawDebugHUD();
    
    void togglePass(int index) { if (index >= 0 && index < 9) passes[index].enabled = !passes[index].enabled; }
    
    void setWindowTitle(const std::string& title);
    int getRefreshRate();
    std::string getRendererBackend();

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void frameUpdate() { frameCounter++; textCache.cleanupOldTextures(); logCache.cleanupOldTextures(); }
    void preloadCommonText();

private:
    void drawText(int x, int y, const std::string& text, SDL_Color color, int fontSize = 24);
    void drawTime(int x, int y, int seconds);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    TTF_Font* logFont = nullptr;
    
    int width = 1280;
    int height = 720;
    double totalFrameTimeMs = 0.0;
    uint64_t frameCounter = 0;
    
    TextCache textCache;
    TextCache logCache;
    bool cachesInitialized = false;
    
    std::string lastLogHash = "";
    
    RenderPass passes[9] = {
        {"1. Background"},
        {"2. Waveform"},
        {"3. Beat Highlight"},
        {"4. Beatgrid Lines"},
        {"5. Beatgrid Text"},
        {"6. Playhead"},
        {"7. Info Text"},
        {"8. Logs"},
        {"9. Debug HUD"}
    };
};
