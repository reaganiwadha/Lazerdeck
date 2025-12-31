#include "Renderer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

TextCache::TextCache(SDL_Renderer* renderer, TTF_Font* font) 
    : renderer(renderer), font(font), frameCounter(0) {}

TextCache::~TextCache() {
    for (auto& pair : cache) {
        if (pair.second.texture) {
            SDL_DestroyTexture(pair.second.texture);
        }
    }
}

SDL_Texture* TextCache::get(const std::string& text, SDL_Color color, int fontSize) {
    // Create cache key: text + color + fontSize
    std::ostringstream key;
    key << text << "|" << (int)color.r << "," << (int)color.g << "," << (int)color.b << "," << (int)color.a << "|" << fontSize;
    std::string cacheKey = key.str();
    
    frameCounter++;
    
    auto it = cache.find(cacheKey);
    if (it != cache.end()) {
        it->second.lastUsed = frameCounter;
        return it->second.texture;
    }
    
    // Create new texture
    TTF_Font* f = (fontSize == 12 && font != nullptr) ? font : font;
    if (!f) return nullptr;
    
    SDL_Surface* surface = TTF_RenderText_Blended(f, text.c_str(), color);
    if (!surface) return nullptr;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    
    CachedTexture cached;
    cached.texture = texture;
    cached.width = surface->w;
    cached.height = surface->h;
    cached.lastUsed = frameCounter;
    
    cache[cacheKey] = cached;
    
    SDL_FreeSurface(surface);
    return texture;
}

void TextCache::getTextureSize(SDL_Texture* texture, int* w, int* h) {
    SDL_QueryTexture(texture, nullptr, nullptr, w, h);
}

void TextCache::cleanupOldTextures(uint64_t maxAgeFrames) {
    for (auto it = cache.begin(); it != cache.end(); ) {
        if (frameCounter - it->second.lastUsed > maxAgeFrames) {
            if (it->second.texture) {
                SDL_DestroyTexture(it->second.texture);
            }
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

Renderer::Renderer() : textCache(nullptr, nullptr), logCache(nullptr, nullptr) {}

Renderer::~Renderer() {
    if (font) TTF_CloseFont(font);
    if (logFont) TTF_CloseFont(logFont);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}

bool Renderer::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        Logger::error("SDL Error: " + std::string(SDL_GetError()));
        return false;
    }

    if (TTF_Init() == -1) {
        Logger::error("TTF Error: " + std::string(TTF_GetError()));
        return false;
    }

    window = SDL_CreateWindow(
        "Lazerdeck - 2 Deck Mixer",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN
    );

    if (!window) return false;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return false;

    // Load fonts
    font = TTF_OpenFont("resources/JetBrainsMono-Regular.ttf", 24);
    if (!font) {
        Logger::error("Failed to load font: " + std::string(TTF_GetError()));
    }
    
    logFont = TTF_OpenFont("resources/JetBrainsMono-Regular.ttf", 12);
    if (!logFont) {
        Logger::error("Failed to load log font: " + std::string(TTF_GetError()));
    }

    // Initialize text caches
    textCache = TextCache(renderer, font);
    logCache = TextCache(renderer, logFont);
    cachesInitialized = true;
    
    // Pre-render common text (beat numbers 1-200)
    preloadCommonText();

    return true;
}

void Renderer::clear() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void Renderer::present() {
    auto pass8Start = std::chrono::high_resolution_clock::now();
    if (passes[7].enabled) {
        drawLogs();
    }
    passes[7].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass8Start).count();
    
    drawDebugHUD();
    SDL_RenderPresent(renderer);
}

void Renderer::setWindowTitle(const std::string& title) {
    if (window) {
        SDL_SetWindowTitle(window, title.c_str());
    }
}

int Renderer::getRefreshRate() {
    if (!window) return 60;
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode;
    if (SDL_GetDisplayMode(displayIndex, 0, &mode) == 0) {
        return mode.refresh_rate > 0 ? mode.refresh_rate : 60;
    }
    return 60;
}

std::string Renderer::getRendererBackend() {
    if (!renderer) return "Unknown";
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0) {
        return info.name;
    }
    return "Unknown";
}

void Renderer::drawText(int x, int y, const std::string& text, SDL_Color color, int fontSize) {
    if (!cachesInitialized) return;
    
    TextCache* cache = (fontSize == 12) ? &logCache : &textCache;
    SDL_Texture* texture = cache->get(text, color, fontSize);
    if (!texture) return;
    
    int w, h;
    cache->getTextureSize(texture, &w, &h);
    SDL_Rect dest = {x, y, w, h};
    
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
}

void Renderer::preloadCommonText() {
    SDL_Color white = {255, 255, 255, 200};
    SDL_Color whiteFull = {255, 255, 255, 255};
    SDL_Color cyan = {0, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color gray = {100, 100, 100, 255};
    
    // Pre-render beat numbers 1-200
    for (int i = 1; i <= 200; i++) {
        textCache.get(std::to_string(i), white, 24);
    }
    
    // Pre-render common UI text
    textCache.get("Preparing Waveform...", whiteFull, 24);
    textCache.get("Analyzing BPM...", gray, 24);
    textCache.get("METRONOME", green, 24);
    textCache.get("Loading...", yellow, 24);
}

void Renderer::drawLogs() {
    auto entries = Logger::getInstance().getEntries();
    int y = height - 20;
    int x = width - 400;

    // Limit log entries drawn for performance
    const int maxLogEntries = 5;
    int entriesToDraw = std::min((int)entries.size(), maxLogEntries);

    // Draw background for logs
    if (entriesToDraw > 0) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        SDL_Rect bg = {x - 10, height - entriesToDraw * 14 - 10, 410, entriesToDraw * 14 + 10};
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Draw in reverse order (newest at bottom), but limit count
    int count = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && count < entriesToDraw; ++it, ++count) {
        SDL_Color color = {200, 200, 200, 255};
        if (it->level == LogLevel::LevelWarning) color = {255, 200, 0, 255};
        if (it->level == LogLevel::LevelError) color = {255, 50, 50, 255};
        
        drawText(x, y, it->message, color, 12);
        y -= 14;
    }
}

void Renderer::drawDebugHUD() {
    auto hudStart = std::chrono::high_resolution_clock::now();
    
    if (!passes[8].enabled) {
        return;
    }
    
    // Calculate total time
    totalFrameTimeMs = 0.0;
    for (int i = 0; i < 9; i++) {
        totalFrameTimeMs += passes[i].lastTimeMs;
    }
    
    // Draw background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect bg = {10, 10, 280, 175};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Draw title and total
    drawText(15, 15, "RENDER PASSES (1-9 toggle)", {0, 255, 255, 255}, 12);
    char totalBuf[64];
    snprintf(totalBuf, sizeof(totalBuf), "Total Frame: %.3f ms", totalFrameTimeMs);
    drawText(15, 30, totalBuf, {255, 255, 0, 255}, 12);

    int y = 50;
    for (int i = 0; i < 9; i++) {
        SDL_Color color = passes[i].enabled ? SDL_Color{0, 255, 0, 255} : SDL_Color{255, 100, 100, 255};
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %.3f ms", passes[i].name, passes[i].lastTimeMs);
        drawText(15, y, buf, color, 12);
        y += 14;
    }
    
    passes[8].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - hudStart).count();
}

void Renderer::drawTime(int x, int y, int seconds) {
    int m = seconds / 60;
    int s = seconds % 60;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", m, s);
    drawText(x, y, buffer, {255, 255, 255, 255});
}

void Renderer::renderDeck(Deck* deck, int yOffset, int height, int samplesPerPixel, const char* name, bool isActive) {
    if (!deck) return;

    auto passStart = std::chrono::high_resolution_clock::now();

    // Draw active border
    if (isActive) {
        SDL_SetRenderDrawColor(renderer, 0, 100, 255, 255);
        SDL_Rect border = {0, yOffset, width, height};
        SDL_RenderDrawRect(renderer, &border);
        border.x += 1; border.y += 1; border.w -= 2; border.h -= 2;
        SDL_RenderDrawRect(renderer, &border);
    }

    const AudioBuffer& buffer = deck->getBuffer();
    double currentFrame = deck->getVisualFrame();
    uint64_t framesAvailable = deck->getFramesAvailable();
    const FFTEnergy& fftEnergy = deck->getFFTEnergy();

    double speed = deck->getSpeed();
    if (speed < 0.01) speed = 0.01;
    double stretchFactor = 1.0 / speed;
    float effectiveSPP = (float)samplesPerPixel / (float)stretchFactor;
    if (effectiveSPP < 1.0f) effectiveSPP = 1.0f;

    int halfWidth = width / 2;
    int centerY = yOffset + height / 2;
    int scale = height / 2;

    // Pass 1: Background
    auto pass1Start = std::chrono::high_resolution_clock::now();
    if (passes[0].enabled) {
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_Rect bgRect = {2, yOffset + 2, width - 4, height - 4};
        SDL_RenderFillRect(renderer, &bgRect);
    }
    passes[0].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass1Start).count();

    // If loading
    if (deck->isLoading() && framesAvailable == 0) {
        drawText(halfWidth - 100, centerY - 12, "Preparing Waveform...", {255, 255, 255, 255});
        return; 
    }

    // Pass 2: Waveform
    auto pass2Start = std::chrono::high_resolution_clock::now();
    if (passes[1].enabled) {
        int startPixel = (int)(halfWidth - currentFrame / effectiveSPP);
        if (startPixel < 0) startPixel = 0;

        // Only update color every N pixels to reduce mutex contention
        const int colorUpdateInterval = 8;
        uint8_t currentR = 255, currentG = 255, currentB = 255;

        for (int x = startPixel; x < width; x++) {
            double frameCenter = (double)currentFrame + ((double)x - halfWidth) * effectiveSPP;
            
            if (frameCenter < 0) continue;
            
            uint64_t frameStart = (uint64_t)frameCenter;
            uint64_t frameEnd = (uint64_t)(frameCenter + effectiveSPP);
            
            if (frameEnd <= frameStart) frameEnd = frameStart + 1;

            if (frameStart >= framesAvailable) break;
            if (frameEnd > framesAvailable) frameEnd = framesAvailable;
            
            if (effectiveSPP > 1.0f) {
                 float minPeak = 1.0f;
                 float maxPeak = -1.0f;
                 
                 int stride = 1;
                 if (frameEnd - frameStart > 100) stride = (frameEnd - frameStart) / 100;

                  for (uint64_t f = frameStart; f < frameEnd; f += stride) {
                      float s = buffer.sample(f, 0); 
                      if (s < minPeak) minPeak = s;
                      if (s > maxPeak) maxPeak = s;
                  }
                 
                 if (minPeak > maxPeak) {
                     minPeak = 0; maxPeak = 0;
                 }

                 int yMin = (int)(minPeak * (scale * 0.9f) + centerY);
                 int yMax = (int)(maxPeak * (scale * 0.9f) + centerY);
                 
                 // Update color less frequently
                 if (x % colorUpdateInterval == 0) {
                     fftEnergy.getColorAtFrame(frameStart, currentR, currentG, currentB);
                 }
                 
                 SDL_SetRenderDrawColor(renderer, currentR, currentG, currentB, 255);
                 SDL_RenderDrawLine(renderer, x, yMax, x, yMin);
                 
            } else {
                 float s = buffer.sample(frameStart, 0);
                 int y = (int)(s * (scale * 0.9f) + centerY);
                 SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                 SDL_RenderDrawPoint(renderer, x, y);
            }
        }
    }
    passes[1].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass2Start).count();
    
    // Beat Background Highlight (Pass 3)
    float gridBpm = deck->getBPM();
    auto pass3Start = std::chrono::high_resolution_clock::now();
    if (passes[2].enabled && gridBpm > 0.0f) {
        float beatOffset = deck->getBeatOffset();
        double framesPerBeat = (double)buffer.sampleRate * 60.0 / (double)gridBpm;
        
        // Calculate current beat index
        int64_t currentBeatIndex = (int64_t)floor(((double)currentFrame - beatOffset) / framesPerBeat);
        double startFrameOfBeat = currentBeatIndex * framesPerBeat + beatOffset;
        double endFrameOfBeat = (currentBeatIndex + 1) * framesPerBeat + beatOffset;
        
        // Progress within the beat [0, 1]
        double progress = ((double)currentFrame - startFrameOfBeat) / framesPerBeat;
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;

        // Color from current frame energy
        uint8_t r, g, b;
        fftEnergy.getColorAtFrame((uint64_t)currentFrame, r, g, b);
        
        // Calculate opacity (fades out as it moves into the beat)
        uint8_t alpha = (uint8_t)((1.0 - progress) * 100);

        // Render background rect for the beat
        int beatXStart = halfWidth + (int)((startFrameOfBeat - (double)currentFrame) / effectiveSPP);
        int beatXEnd = halfWidth + (int)((endFrameOfBeat - (double)currentFrame) / effectiveSPP);

        // Clamp to screen
        if (beatXStart < 0) beatXStart = 0;
        if (beatXEnd > width) beatXEnd = width;

        if (beatXEnd > beatXStart) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
            SDL_Rect bgRect = { beatXStart, yOffset, beatXEnd - beatXStart, height };
            SDL_RenderFillRect(renderer, &bgRect);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
    }
    passes[2].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass3Start).count();
    
    // BeatGrid Visualization (Pass 4: Lines, Pass 5: Text)
    auto pass4Start = std::chrono::high_resolution_clock::now();
    if (passes[3].enabled && gridBpm > 0.0f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 60);

        float beatOffset = deck->getBeatOffset();
        double framesPerBeat = (double)buffer.sampleRate * 60.0 / (double)gridBpm;
        
        double startFrame = (double)currentFrame - (double)halfWidth * effectiveSPP;
        double endFrame = (double)currentFrame + (double)(width - halfWidth) * effectiveSPP;
        
        int64_t startBeatIndex = (int64_t)floor((startFrame - beatOffset) / framesPerBeat);
        
        for (int64_t i = startBeatIndex; ; ++i) {
            double beatFrame = i * framesPerBeat + beatOffset;
            
            if (beatFrame > endFrame) break;
            
            double diff = beatFrame - (double)currentFrame;
            int x = halfWidth + (int)(diff / effectiveSPP);
            
            if (x >= -2 && x < width + 2) {
                SDL_Rect rect = { x - 2, yOffset, 4, height };
                SDL_RenderFillRect(renderer, &rect);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
    passes[3].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass4Start).count();
    
    auto pass5Start = std::chrono::high_resolution_clock::now();
    if (passes[4].enabled && gridBpm > 0.0f) {
        float beatOffset = deck->getBeatOffset();
        double framesPerBeat = (double)buffer.sampleRate * 60.0 / (double)gridBpm;
        
        double startFrame = (double)currentFrame - (double)halfWidth * effectiveSPP;
        double endFrame = (double)currentFrame + (double)(width - halfWidth) * effectiveSPP;
        
        int64_t startBeatIndex = (int64_t)floor((startFrame - beatOffset) / framesPerBeat);
        
        for (int64_t i = startBeatIndex; ; ++i) {
            double beatFrame = i * framesPerBeat + beatOffset;
            
            if (beatFrame > endFrame) break;
            
            double diff = beatFrame - (double)currentFrame;
            int x = halfWidth + (int)(diff / effectiveSPP);
            
            if (x >= -2 && x < width + 2) {
                std::string beatNum = std::to_string(i + 1);
                drawText(x + 4, yOffset + height - 25, beatNum, {255, 255, 255, 200});
            }
        }
    }
    passes[4].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass5Start).count();
    
    // Draw Playhead (Pass 6)
    auto pass6Start = std::chrono::high_resolution_clock::now();
    if (passes[5].enabled) {
        static SDL_BlendMode invertMode = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT);
            
        SDL_SetRenderDrawBlendMode(renderer, invertMode);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, halfWidth, yOffset, halfWidth, yOffset + height);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
    passes[5].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass6Start).count();
    
    // Info Text (Pass 7)
    auto pass7Start = std::chrono::high_resolution_clock::now();
    if (passes[6].enabled) {
        SDL_Color statusColor = deck->isPlaying() ? SDL_Color{0, 255, 0, 255} : SDL_Color{255, 255, 0, 255};
        SDL_Rect statusRect = {10, yOffset + 10, 10, 10};
        SDL_SetRenderDrawColor(renderer, statusColor.r, statusColor.g, statusColor.b, statusColor.a);
        SDL_RenderFillRect(renderer, &statusRect);
        
        drawText(30, yOffset + 5, name, {255, 255, 255, 255});

        if (buffer.sampleRate > 0) {
            int currentSeconds = (int)(currentFrame / buffer.sampleRate);
            int totalSeconds = (int)(framesAvailable / buffer.sampleRate);
            drawTime(30, yOffset + 35, currentSeconds);
            drawText(90, yOffset + 35, "/", {200, 200, 200, 255});
            drawTime(105, yOffset + 35, totalSeconds);
        }

        float ratio = (float)(speed - 1.0) * 100.0f;
        float originalBPM = deck->getBPM();
        float effectiveBPM = deck->getEffectiveBPM();
        float offset = deck->getBeatOffset();

        char rateBuf[256];
        if (originalBPM > 0.0f) {
             snprintf(rateBuf, sizeof(rateBuf), "Stretch: %.2fx | Original BPM: %.1f | Effective BPM: %.1f | Offset: %.3f | Rate: %+.1f%%", stretchFactor, originalBPM, effectiveBPM, offset, ratio);
        } else {
             snprintf(rateBuf, sizeof(rateBuf), "Stretch: %.2fx | Rate: %+.1f%%", stretchFactor, ratio);
        }
        drawText(halfWidth + 20, yOffset + 35, rateBuf, {0, 255, 255, 255});

        if (deck->isAnalyzing()) {
            drawText(halfWidth + 20, yOffset + 60, "Analyzing BPM...", {100, 100, 100, 255});
        }

        if (deck->isMetronomeEnabled()) {
            drawText(halfWidth + 20, yOffset + 85, "METRONOME", {0, 255, 0, 255});
        }

        if (deck->isLoading()) {
            drawText(width - 250, yOffset + 10, "Loading...", {255, 255, 0, 255});
        }
    }
    passes[6].lastTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - pass7Start).count();
}