#include "AudioEngine.hpp"
#include <algorithm>
#include <cmath>
#include "Logger.hpp"

AudioEngine::AudioEngine() : stream(nullptr), initialized(false), sampleRate(44100) {
    mixState.deckA = nullptr;
    mixState.deckB = nullptr;
}

AudioEngine::~AudioEngine() {
    stop();
    if (initialized) {
        Pa_Terminate();
    }
}

bool AudioEngine::init(Deck* deckA, Deck* deckB, int sampleRate) {
    this->sampleRate = sampleRate;
    mixState.deckA = deckA;
    mixState.deckB = deckB;
    mixState.engine = this;

    // Generate metronome click
    metronomeClick.resize(sampleRate * 0.05); // 50ms
    for (size_t i = 0; i < metronomeClick.size(); ++i) {
        float t = (float)i / (float)sampleRate;
        float envelope = 1.0f - ((float)i / metronomeClick.size());
        metronomeClick[i] = (std::sin(2.0f * 3.14159f * 1000.0f * t) * 0.5f + 
                             std::sin(2.0f * 3.14159f * 2000.0f * t) * 0.25f) * envelope * 0.8f;
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        Logger::error("PortAudio error: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    initialized = true;

    err = Pa_OpenDefaultStream(
        &stream,
        0,                      // no input
        2,                      // 2 output channels (Stereo)
        paFloat32,              // 32-bit float
        sampleRate,             // Dynamic sample rate
        256,                    // frames per buffer
        audioCallback,
        &mixState
    );

    if (err != paNoError) {
        Logger::error("PortAudio stream error: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    return true;
}

bool AudioEngine::start() {
    if (!stream) return false;
    PaError err = Pa_StartStream(stream);
    if (err != paNoError) {
        Logger::error("Failed to start stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    return true;
}

void AudioEngine::stop() {
    if (stream) {
        if (Pa_IsStreamActive(stream)) {
            Pa_StopStream(stream);
        }
        Pa_CloseStream(stream);
        stream = nullptr;
    }
}

int AudioEngine::audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData)
{
    MixState *mix = (MixState*)userData;
    float *out = (float*)outputBuffer;
    
    (void) timeInfo;
    (void) statusFlags;
    (void) inputBuffer;
    
    // Clear output buffer first (silence)
    std::fill(out, out + framesPerBuffer * 2, 0.0f);
    
    // Mix Deck A
    if (mix->deckA) {
        mix->deckA->process(out, framesPerBuffer);
    }
    
    // Mix Deck B
    if (mix->deckB) {
        mix->deckB->process(out, framesPerBuffer);
    }
    
    // Mix Metronomes (separate path)
    if (mix->engine) {
        mix->engine->renderMetronome(out, framesPerBuffer);
    }
    
    return paContinue;
}

void AudioEngine::renderMetronome(float* outputBuffer, unsigned long framesPerBuffer) {
    if (mixState.deckA) {
        processDeckMetronome(mixState.deckA, metronomeA, outputBuffer, framesPerBuffer);
    }
    if (mixState.deckB) {
        processDeckMetronome(mixState.deckB, metronomeB, outputBuffer, framesPerBuffer);
    }
}

void AudioEngine::processDeckMetronome(Deck* deck, DeckMetronome& state, float* outputBuffer, unsigned long framesPerBuffer) {
    if (!deck->isMetronomeEnabled()) {
        state.clickCursor = -1;
        return;
    }

    float bpm = deck->getBPM();
    if (bpm <= 0.0f) return;

    double timePerBeat = 60.0 / (double)bpm;
    double offsetTime = (double)deck->getBeatOffset() / (double)sampleRate;
    double currentInputTime = deck->getCurrentInputTime();
    double s = deck->getSpeed();

    // Re-sync nextBeatTime if it's way off or uninitialized
    if (state.nextBeatTime < 0 || std::abs(currentInputTime - state.nextBeatTime) > 2.0 * timePerBeat) {
        int64_t k = (int64_t)ceil((currentInputTime - offsetTime) / timePerBeat - 0.0001);
        state.nextBeatTime = offsetTime + (double)k * timePerBeat;
    }

    for (size_t i = 0; i < framesPerBuffer; ++i) {
        // Check if currentInputTime crossed nextBeatTime
        // We use a local input time approximation for this buffer
        double localInputTime = currentInputTime + (double)i / (double)sampleRate * s;

        if (localInputTime >= state.nextBeatTime) {
            state.clickCursor = 0;
            state.nextBeatTime += timePerBeat;
            // Catch up if needed
            while (localInputTime >= state.nextBeatTime) {
                state.nextBeatTime += timePerBeat;
            }
        }

        if (state.clickCursor >= 0) {
            if (state.clickCursor < (int)metronomeClick.size()) {
                float clickVal = metronomeClick[state.clickCursor++];
                outputBuffer[i * 2 + 0] += clickVal;
                outputBuffer[i * 2 + 1] += clickVal;
            } else {
                state.clickCursor = -1;
            }
        }
    }
}
