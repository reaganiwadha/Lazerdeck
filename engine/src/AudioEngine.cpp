#include "AudioEngine.hpp"
#include <algorithm>
#include <cmath>
#include "Logger.hpp"

AudioEngine::AudioEngine() : stream(nullptr), initialized(false), sampleRate(44100), framesPerBuffer(0), latencyMs(0), actualSampleRate(44100) {
    mixState.engine = nullptr;
}

AudioEngine::~AudioEngine() {
    stop();
    if (initialized) {
        Pa_Terminate();
    }
}

bool AudioEngine::init(const std::vector<Deck*>& decks, Lazerdeck::Mixer* mixer, int sampleRate, int bufferSize) {
    this->sampleRate = sampleRate;
    mixState.decks = decks;
    mixState.mixer = mixer;
    mixState.engine = this;
    framesPerBuffer = bufferSize;

    metronomeStates.assign(decks.size(), DeckMetronome{});

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

    // Configure for low latency
    PaStreamParameters outputParams;
    PaDeviceIndex deviceIndex = paNoDevice;
    
#ifdef _WIN32
    // Try WASAPI first on Windows for lowest latency
    PaHostApiIndex wasapiIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (wasapiIndex >= 0) {
        const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(wasapiIndex);
        deviceIndex = hostInfo->defaultOutputDevice;
        Logger::info("Using WASAPI for low latency");
    }
#endif

    // Fallback to default device if WASAPI not available
    if (deviceIndex == paNoDevice) {
        deviceIndex = Pa_GetDefaultOutputDevice();
        Logger::info("Using default audio API");
    }

    if (deviceIndex == paNoDevice) {
        Logger::error("No default output device");
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    Logger::info("Audio device: " + std::string(deviceInfo->name));
    Logger::info("Device default sample rate: " + std::to_string(deviceInfo->defaultSampleRate));
    Logger::info("Suggested low latency: " + std::to_string(deviceInfo->defaultLowOutputLatency * 1000.0) + "ms");

    outputParams.device = deviceIndex;
    outputParams.channelCount = 2;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = NULL;

    // Try to open stream with requested sample rate
    err = Pa_OpenStream(
        &stream,
        nullptr,
        &outputParams,
        sampleRate,
        framesPerBuffer,
        paClipOff,
        audioCallback,
        &mixState
    );

    // If sample rate not supported, try common rates
    if (err == paInvalidSampleRate || err == paUnanticipatedHostError) {
        Logger::info("Requested sample rate " + std::to_string(sampleRate) + " not supported");
        
        // Try common sample rates in order of preference for low latency
        int fallbackRates[] = {48000, 44100, 96000, 192000, 88200};
        bool opened = false;
        
        for (int rate : fallbackRates) {
            if (rate == sampleRate) continue; // Already tried
            
            Logger::info("Trying sample rate: " + std::to_string(rate));
            err = Pa_OpenStream(
                &stream,
                nullptr,
                &outputParams,
                rate,
                framesPerBuffer,
                paClipOff,
                audioCallback,
                &mixState
            );
            
            if (err == paNoError) {
                this->sampleRate = rate;
                Logger::info("Successfully opened stream at " + std::to_string(rate) + " Hz");
                
                // Regenerate metronome click with new sample rate
                metronomeClick.resize(this->sampleRate * 0.05);
                for (size_t i = 0; i < metronomeClick.size(); ++i) {
                    float t = (float)i / (float)this->sampleRate;
                    float envelope = 1.0f - ((float)i / metronomeClick.size());
                    metronomeClick[i] = (std::sin(2.0f * 3.14159f * 1000.0f * t) * 0.5f + 
                                         std::sin(2.0f * 3.14159f * 2000.0f * t) * 0.25f) * envelope * 0.8f;
                }
                opened = true;
                break;
            }
        }
        
        if (!opened) {
            Logger::error("Could not find a supported sample rate");
            Logger::error("Last PortAudio error: " + std::string(Pa_GetErrorText(err)));
            return false;
        }
    } else if (err != paNoError) {
        Logger::error("PortAudio stream error: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    // Get actual latency and sample rate
    const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream);
    if (streamInfo) {
        actualSampleRate = (int)streamInfo->sampleRate;
        latencyMs = (int)(streamInfo->outputLatency * 1000.0);
        Logger::info("Audio buffer size: " + std::to_string(framesPerBuffer) + " frames");
        Logger::info("Actual audio latency: " + std::to_string(latencyMs) + "ms");
        Logger::info("Requested sample rate: " + std::to_string(sampleRate) + " Hz");
        Logger::info("Actual sample rate: " + std::to_string(actualSampleRate) + " Hz");
        
        if (actualSampleRate != sampleRate) {
            Logger::warn("Sample rate mismatch! Device running at " + std::to_string(actualSampleRate) + " Hz instead of " + std::to_string(sampleRate) + " Hz");
            
            this->sampleRate = actualSampleRate;

            // Regenerate metronome click with new sample rate
            metronomeClick.resize(this->sampleRate * 0.05);
            for (size_t i = 0; i < metronomeClick.size(); ++i) {
                float t = (float)i / (float)this->sampleRate;
                float envelope = 1.0f - ((float)i / metronomeClick.size());
                metronomeClick[i] = (std::sin(2.0f * 3.14159f * 1000.0f * t) * 0.5f + 
                                     std::sin(2.0f * 3.14159f * 2000.0f * t) * 0.25f) * envelope * 0.8f;
            }

            // Update decks to use actual sample rate
            for (auto* deck : mixState.decks) {
                if (deck) deck->updateSampleRate(actualSampleRate);
            }
            if (mixState.mixer) {
                mixState.mixer->setSampleRate(actualSampleRate);
            }
        }
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

// int AudioEngine::getSampleRate() const {
//     return sampleRate;
// }

int AudioEngine::audioCallback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData) {
    MixState *mix = (MixState*)userData;
    float *out = (float*)outputBuffer;
    
    (void) statusFlags;
    (void) inputBuffer;
    
    // Clear output buffer first (silence)
    std::fill(out, out + framesPerBuffer * 2, 0.0f);
    
    // Temp buffer for deck output (raw)
    static std::vector<float> deckBuffer;
    if (deckBuffer.size() < framesPerBuffer * 2) {
        deckBuffer.resize(framesPerBuffer * 2);
    }

    // Process Decks and Mix
    int deckIdx = 0;
    for (auto* deck : mix->decks) {
        if (deck) {
            // Clear deck buffer
            std::fill(deckBuffer.begin(), deckBuffer.begin() + framesPerBuffer * 2, 0.0f);
            
            // Get raw audio from Deck (overwrite deckBuffer)
            // Note: Deck::process adds to buffer, so we cleared it first.
            deck->process(deckBuffer.data(), framesPerBuffer);

            // Process via Mixer Channel
            if (mix->mixer) {
                auto* channel = mix->mixer->getChannel(deckIdx);
                if (channel) {
                    channel->process(deckBuffer.data(), out, framesPerBuffer);
                } else {
                    // Fallback: just add to output if no channel
                    for (unsigned long i = 0; i < framesPerBuffer * 2; ++i) {
                        out[i] += deckBuffer[i];
                    }
                }
            } else {
                // Fallback: just add to output if no mixer
                for (unsigned long i = 0; i < framesPerBuffer * 2; ++i) {
                    out[i] += deckBuffer[i];
                }
            }
        }
        deckIdx++;
    }
    
    // Mix Metronomes (separate path)
    if (mix->engine) {
        mix->engine->renderMetronome(out, framesPerBuffer);
    }
    
    // Calculate latency
    if (timeInfo && mix->engine) {
        int latency = (int)(timeInfo->outputBufferDacTime * 1000.0);
        mix->engine->latencyMs = latency;
    }
    
    return paContinue;
}

void AudioEngine::renderMetronome(float* outputBuffer, unsigned long framesPerBuffer) {
    for (size_t i = 0; i < mixState.decks.size(); ++i) {
        if (mixState.decks[i]) {
            processDeckMetronome(mixState.decks[i], metronomeStates[i], outputBuffer, framesPerBuffer);
        }
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