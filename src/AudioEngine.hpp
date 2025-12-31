#pragma once

#include <portaudio.h>
#include <vector>
#include <iostream>
#include "Deck.hpp"

class AudioEngine;

struct MixState {
    Deck* deckA;
    Deck* deckB;
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

    bool init(Deck* deckA, Deck* deckB, int sampleRate = 44100);
    bool start();
    void stop();

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

    std::vector<float> metronomeClick;
    DeckMetronome metronomeA;
    DeckMetronome metronomeB;
    int sampleRate;
};
