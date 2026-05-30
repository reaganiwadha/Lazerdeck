#pragma once

#include <portaudio.h>
#include <vector>
#include <string>
#include <mutex>
#include <iostream>
#include "Deck.hpp"
#include "Mixer.hpp"

class AudioEngine;

// One selectable output device, as surfaced to the host UI.
struct AudioDeviceInfo {
    int         index = -1;          // PortAudio device index
    std::string name;
    std::string hostApi;
    int         maxOutputChannels = 0;
    double      defaultSampleRate = 0.0;
    bool        isDefault = false;   // host API's default output device
};

struct MixState {
    std::vector<Deck*> decks;
    Lazerdeck::Mixer* mixer = nullptr;
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

    bool init(const std::vector<Deck*>& decks, Lazerdeck::Mixer* mixer, int sampleRate = 44100, int bufferSize = 128);
    bool start();
    void stop();

    // Switch output to `deviceIndex`, reopening the stream in place. MUST be
    // called from the engine thread (not the audio callback). Decks/mixer are
    // retargeted to the new device's sample rate. Returns true on success.
    // When `requestedRate > 0`, the stream is (re)opened at that sample rate
    // (subject to the device's supported-rate fallback); 0 keeps the current.
    bool reopen(int deviceIndex, int requestedRate = 0);

    // Re-enumerates output-capable devices, caches them, and returns the list.
    std::vector<AudioDeviceInfo> refreshDevices();
    int  getDeviceCacheSize() const;
    bool getCachedDevice(int listIndex, AudioDeviceInfo& out) const;

    int getBufferSize() const { return framesPerBuffer; }
    int getBitDepth() const { return 32; } // paFloat32 = 32-bit
    int getLatencyMs() const { return latencyMs; }
    int getActualSampleRate() const { return actualSampleRate; }
    int getCurrentDevice() const { return (int)currentDevice; }
    std::string getCurrentDeviceName() const;
    std::string getCurrentHostApi() const;

private:
    // Opens a stream on `deviceIndex` (with sample-rate fallback) and retargets
    // decks/mixer. Does not start the stream. Caller serializes via paMutex.
    bool openStream(PaDeviceIndex deviceIndex);
    void regenerateMetronome();

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
    PaDeviceIndex currentDevice = paNoDevice;

    // Serializes stream open/close (reopen) against device enumeration and
    // config reads coming from the UI thread.
    mutable std::mutex paMutex;
    std::vector<AudioDeviceInfo> deviceCache;

    std::vector<float> metronomeClick;
    std::vector<DeckMetronome> metronomeStates;
    int sampleRate;
};
