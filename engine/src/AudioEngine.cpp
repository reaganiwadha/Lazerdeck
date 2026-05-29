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
    regenerateMetronome();

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        Logger::error("PortAudio error: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    initialized = true;

    // Pick the initial output device.
    PaDeviceIndex deviceIndex = paNoDevice;
#ifdef _WIN32
    // Prefer WASAPI on Windows for lowest latency.
    PaHostApiIndex wasapiIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (wasapiIndex >= 0) {
        const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(wasapiIndex);
        deviceIndex = hostInfo->defaultOutputDevice;
        Logger::info("Using WASAPI for low latency");
    }
#endif
    if (deviceIndex == paNoDevice) {
        deviceIndex = Pa_GetDefaultOutputDevice();
        Logger::info("Using default audio API");
    }
    if (deviceIndex == paNoDevice) {
        Logger::error("No default output device");
        return false;
    }

    return openStream(deviceIndex);
}

void AudioEngine::regenerateMetronome() {
    metronomeClick.resize((size_t)(sampleRate * 0.05)); // 50ms
    for (size_t i = 0; i < metronomeClick.size(); ++i) {
        float t = (float)i / (float)sampleRate;
        float envelope = 1.0f - ((float)i / metronomeClick.size());
        metronomeClick[i] = (std::sin(2.0f * 3.14159f * 1000.0f * t) * 0.5f +
                             std::sin(2.0f * 3.14159f * 2000.0f * t) * 0.25f) * envelope * 0.8f;
    }
}

bool AudioEngine::openStream(PaDeviceIndex deviceIndex) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (!deviceInfo) {
        Logger::error("openStream: invalid device index " + std::to_string(deviceIndex));
        return false;
    }
    Logger::info("Audio device: " + std::string(deviceInfo->name));

    PaStreamParameters outputParams;
    outputParams.device = deviceIndex;
    outputParams.channelCount = 2;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = NULL;

    PaStream* newStream = nullptr;
    PaError err = Pa_OpenStream(&newStream, nullptr, &outputParams, sampleRate,
                                framesPerBuffer, paClipOff, audioCallback, &mixState);

    // Fall back through common rates if the requested one isn't supported.
    if (err == paInvalidSampleRate || err == paUnanticipatedHostError) {
        Logger::info("Sample rate " + std::to_string(sampleRate) + " not supported; trying fallbacks");
        int fallbackRates[] = {48000, 44100, 96000, 192000, 88200};
        bool opened = false;
        for (int rate : fallbackRates) {
            if (rate == sampleRate) continue;
            err = Pa_OpenStream(&newStream, nullptr, &outputParams, rate,
                                framesPerBuffer, paClipOff, audioCallback, &mixState);
            if (err == paNoError) {
                sampleRate = rate;
                opened = true;
                break;
            }
        }
        if (!opened) {
            Logger::error("No supported sample rate: " + std::string(Pa_GetErrorText(err)));
            return false;
        }
    } else if (err != paNoError) {
        Logger::error("PortAudio stream error: " + std::string(Pa_GetErrorText(err)));
        return false;
    }

    stream = newStream;
    currentDevice = deviceIndex;

    // Reconcile against what the device actually opened at.
    const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream);
    if (streamInfo) {
        actualSampleRate = (int)streamInfo->sampleRate;
        latencyMs = (int)(streamInfo->outputLatency * 1000.0);
        if (actualSampleRate != sampleRate) {
            Logger::warn("Sample rate mismatch: device at " + std::to_string(actualSampleRate) +
                         " Hz, requested " + std::to_string(sampleRate) + " Hz");
            sampleRate = actualSampleRate;
        }
    }

    // Retarget everything that depends on the sample rate.
    regenerateMetronome();
    for (auto* deck : mixState.decks) {
        if (deck) deck->updateSampleRate(sampleRate);
    }
    if (mixState.mixer) mixState.mixer->setSampleRate(sampleRate);

    Logger::info("Stream open: " + std::string(deviceInfo->name) + " @ " +
                 std::to_string(sampleRate) + " Hz, " + std::to_string(framesPerBuffer) +
                 " frames, ~" + std::to_string(latencyMs) + "ms latency");
    return true;
}

bool AudioEngine::reopen(int deviceIndex) {
    std::lock_guard<std::mutex> lock(paMutex);
    if (!initialized) return false;

    Logger::info("Reopening audio on device index " + std::to_string(deviceIndex));

    // Tear down the current stream first; Pa_StopStream/CloseStream block until
    // the callback has finished, so the device swap is race-free.
    if (stream) {
        if (Pa_IsStreamActive(stream)) Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = nullptr;
    }

    if (!openStream((PaDeviceIndex)deviceIndex)) {
        Logger::error("reopen: failed to open device " + std::to_string(deviceIndex));
        return false;
    }

    PaError err = Pa_StartStream(stream);
    if (err != paNoError) {
        Logger::error("reopen: failed to start stream: " + std::string(Pa_GetErrorText(err)));
        return false;
    }
    return true;
}

std::vector<AudioDeviceInfo> AudioEngine::refreshDevices() {
    std::lock_guard<std::mutex> lock(paMutex);
    deviceCache.clear();
    if (!initialized) return deviceCache;

    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels < 1) continue; // output-capable only

        const PaHostApiInfo* host = Pa_GetHostApiInfo(info->hostApi);
        AudioDeviceInfo d;
        d.index = i;
        d.name = info->name ? info->name : "";
        d.hostApi = (host && host->name) ? host->name : "";
        d.maxOutputChannels = info->maxOutputChannels;
        d.defaultSampleRate = info->defaultSampleRate;
        d.isDefault = (host && host->defaultOutputDevice == i);
        deviceCache.push_back(std::move(d));
    }
    return deviceCache;
}

int AudioEngine::getDeviceCacheSize() const {
    std::lock_guard<std::mutex> lock(paMutex);
    return (int)deviceCache.size();
}

bool AudioEngine::getCachedDevice(int listIndex, AudioDeviceInfo& out) const {
    std::lock_guard<std::mutex> lock(paMutex);
    if (listIndex < 0 || listIndex >= (int)deviceCache.size()) return false;
    out = deviceCache[listIndex];
    return true;
}

std::string AudioEngine::getCurrentDeviceName() const {
    std::lock_guard<std::mutex> lock(paMutex);
    if (!initialized || currentDevice == paNoDevice) return "";
    const PaDeviceInfo* info = Pa_GetDeviceInfo(currentDevice);
    return (info && info->name) ? info->name : "";
}

std::string AudioEngine::getCurrentHostApi() const {
    std::lock_guard<std::mutex> lock(paMutex);
    if (!initialized || currentDevice == paNoDevice) return "";
    const PaDeviceInfo* info = Pa_GetDeviceInfo(currentDevice);
    if (!info) return "";
    const PaHostApiInfo* host = Pa_GetHostApiInfo(info->hostApi);
    return (host && host->name) ? host->name : "";
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