#include "Renderer.hpp"
#include <iostream>

Renderer::Renderer() {}

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

    return true;
}

void Renderer::clear() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void Renderer::present() {
    drawLogs();
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

void Renderer::drawText(int x, int y, const std::string& text, SDL_Color color, int fontSize) {
    TTF_Font* f = (fontSize == 12 && logFont) ? logFont : font;
    if (!f) return;
    
    SDL_Surface* surface = TTF_RenderText_Blended(f, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dest = {x, y, surface->w, surface->h};
    
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
    
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void Renderer::drawLogs() {
    auto entries = Logger::getInstance().getEntries();
    int y = height - 20; // Start from bottom
    int x = width - 400; // Right aligned-ish area

    // Draw background for logs
    if (!entries.empty()) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        SDL_Rect bg = {x - 10, height - (int)entries.size() * 14 - 10, 410, (int)entries.size() * 14 + 10};
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Draw in reverse order (newest at bottom)
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        SDL_Color color = {200, 200, 200, 255};
        if (it->level == LogLevel::LevelWarning) color = {255, 200, 0, 255};
        if (it->level == LogLevel::LevelError) color = {255, 50, 50, 255};
        
        drawText(x, y, it->message, color, 12);
        y -= 14;
    }
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

    // Draw active border
    if (isActive) {
        SDL_SetRenderDrawColor(renderer, 0, 100, 255, 255);
        SDL_Rect border = {0, yOffset, width, height};
        SDL_RenderDrawRect(renderer, &border);
        border.x += 1; border.y += 1; border.w -= 2; border.h -= 2;
        SDL_RenderDrawRect(renderer, &border);
    }

    const AudioBuffer& buffer = deck->getBuffer();
    uint64_t currentFrame = deck->getCurrentFrame();
    uint64_t framesAvailable = deck->getFramesAvailable();
    const FFTEnergy& fftEnergy = deck->getFFTEnergy();

    double speed = deck->getSpeed();
    if (speed < 0.01) speed = 0.01;
    double stretchFactor = 1.0 / speed;
    float effectiveSPP = (float)samplesPerPixel / (float)stretchFactor;
    if (effectiveSPP < 1.0f) effectiveSPP = 1.0f; // Limit zoom

    int halfWidth = width / 2;
    int centerY = yOffset + height / 2;
    int scale = height / 2;

    // Draw background
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_Rect bgRect = {2, yOffset + 2, width - 4, height - 4};
    SDL_RenderFillRect(renderer, &bgRect);

    // If loading
    if (deck->isLoading() && framesAvailable == 0) {
        drawText(halfWidth - 100, centerY - 12, "Preparing Waveform...", {255, 255, 255, 255});
        return; 
    }

    // Draw Waveform
    int startPixel = (int)(halfWidth - currentFrame / effectiveSPP);
    if (startPixel < 0) startPixel = 0;

    for (int x = startPixel; x < width; x++) {
        double frameCenter = (double)currentFrame + ((double)x - halfWidth) * effectiveSPP;
        
        if (frameCenter < 0) continue;
        
        uint64_t frameStart = (uint64_t)frameCenter;
        uint64_t frameEnd = (uint64_t)(frameCenter + effectiveSPP);
        
        if (frameEnd <= frameStart) frameEnd = frameStart + 1; // Ensure at least 1 sample

        if (frameStart >= framesAvailable) break; // Don't draw past loaded
        if (frameEnd > framesAvailable) frameEnd = framesAvailable;
        
        if (effectiveSPP > 1.0f) {
             float minPeak = 1.0f;
             float maxPeak = -1.0f;
             
             // Simple peak finding (with stride if huge range?)
             int stride = 1;
             if (frameEnd - frameStart > 100) stride = (frameEnd - frameStart) / 100;

             for (uint64_t f = frameStart; f < frameEnd; f += stride) {
                 float s = buffer.sample(f, 0); 
                 if (s < minPeak) minPeak = s;
                 if (s > maxPeak) maxPeak = s;
             }
             
             // If silent/empty
             if (minPeak > maxPeak) {
                 minPeak = 0; maxPeak = 0;
             }

             int yMin = (int)(minPeak * (scale * 0.9f) + centerY);
             int yMax = (int)(maxPeak * (scale * 0.9f) + centerY);
             
             uint8_t r, g, b;
             fftEnergy.getColorAtFrame(frameStart, r, g, b);
             SDL_SetRenderDrawColor(renderer, r, g, b, 255);
             SDL_RenderDrawLine(renderer, x, yMax, x, yMin);
             
        } else {
             float s = buffer.sample(frameStart, 0);
             int y = (int)(s * (scale * 0.9f) + centerY);
             SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
             SDL_RenderDrawPoint(renderer, x, y);
        }
    }
    
    // Beat Background Highlight
    float gridBpm = deck->getBPM();
    if (gridBpm > 0.0f) {
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
        fftEnergy.getColorAtFrame(currentFrame, r, g, b);
        
        // Calculate opacity (fades out as it moves into the beat)
        uint8_t alpha = (uint8_t)((1.0 - progress) * 100); // Max alpha 100 for subtlety

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
    
    // BeatGrid Visualization
    if (gridBpm > 0.0f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 60); // Low alpha white with Additive blend

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
                
                // Beat Counter
                std::string beatNum = std::to_string(i + 1);
                drawText(x + 4, yOffset + height - 25, beatNum, {255, 255, 255, 200});
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
    
    // Draw Playhead
    static SDL_BlendMode invertMode = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT);
        
    SDL_SetRenderDrawBlendMode(renderer, invertMode);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White + REV_SUBTRACT = Invert
    SDL_RenderDrawLine(renderer, halfWidth, yOffset, halfWidth, yOffset + height);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    
    // Info
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

    // BPM / Rate Display
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