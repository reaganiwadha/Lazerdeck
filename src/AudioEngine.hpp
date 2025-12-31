#pragma once

#include <portaudio.h>
#include <vector>
#include <iostream>
#include "Deck.hpp"

class AudioEngine;

struct MixState {
    std::vector<Deck*> decks;
    AudioEngine* engine;
};

struct DeckMetronome {
    int clickCursor = -1;
    double nextBeatTime = -1.0;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(const std::vector<Deck*>& decks, int sampleRate = 44100, int bufferSize = 128);
    bool start();
    void stop();
    
    int getBufferSize() const { return framesPerBuffer; }
    int getBitDepth() const { return 32; } // paFloat32 = 32-bit
    int getLatencyMs() const { return latencyMs; }
    int getActualSampleRate() const { return actualSampleRate; }

private:
    static int audioCallback(
        const void *inputBuffer,
        void *outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userData
    );

    void renderMetronome(float* outputBuffer, unsigned long framesPerBuffer);
    void processDeckMetronome(Deck* deck, DeckMetronome& state, float* outputBuffer, unsigned long framesPerBuffer);

    PaStream *stream;
    MixState mixState;
    bool initialized;
    int framesPerBuffer;
    int latencyMs;
    int actualSampleRate;

    std::vector<float> metronomeClick;
    std::vector<DeckMetronome> metronomeStates;
    int sampleRate;
};
