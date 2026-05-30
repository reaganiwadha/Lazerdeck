#include "AudioEngine.hpp"
#include <algorithm>
#include <cmath>
#include "Logger.hpp"

namespace {
// Master-bus soft limiter. Transparent (exactly linear) below the threshold,
// then a tanh knee that asymptotes to the ±1.0 ceiling so summed decks can't
// hard-clip. C1-continuous at the knee (slope 1 on both sides).
inline float softLimit(float x) {
    constexpr float t = 0.8f;                  // linear/transparent below this
    const float a = std::fabs(x);
    if (a <= t) return x;
    constexpr float range = 1.0f - t;          // headroom to the 1.0 ceiling
    const float over = a - t;
    return (x < 0.0f ? -1.0f : 1.0f) * (t + range * std::tanh(over / range));
}
}  // namespace

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

    // Enumerate devices up front so the host UI can offer a picker even when we
    // can't open anything automatically.
    refreshDevices();

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

    // No default device, or the default device won't open: this is NOT fatal.
    // Leave the stream closed and let the user select a working device from the
    // host's audio settings (which calls reopen()). PortAudio stays initialized
    // so device enumeration keeps working.
    if (deviceIndex == paNoDevice) {
        Logger::warn("No default output device; starting with audio disabled. "
                     "Select a device in audio settings.");
        return true;
    }
    if (!openStream(deviceIndex)) {
        Logger::warn("Could not open default output device; starting with audio "
                     "disabled. Select a device in audio settings.");
        return true;
    }

    return true;
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

bool AudioEngine::reopen(int deviceIndex, int requestedRate) {
    std::lock_guard<std::mutex> lock(paMutex);
    if (!initialized) return false;

    // A new requested rate becomes the rate openStream tries first (it still
    // falls back through supported rates if the device rejects it).
    if (requestedRate > 0) sampleRate = requestedRate;

    Logger::info("Reopening audio on device index " + std::to_string(deviceIndex) +
                 " @ " + std::to_string(sampleRate) + " Hz");

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
    // No stream means we started with audio disabled (no usable device yet).
    // That's a valid state — the engine runs and waits for the user to pick a
    // device. Don't treat it as a failure.
    if (!stream) {
        Logger::warn("AudioEngine::start: no audio stream open (audio disabled)");
        return true;
    }
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

    // Master soft-limiter: keep the summed bus from hard-clipping when multiple
    // decks (plus metronome) push past full scale.
    for (unsigned long i = 0; i < framesPerBuffer * 2; ++i) {
        out[i] = softLimit(out[i]);
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
    // One-shot tap click (Tap Tempo wizard): starts a click this buffer even
    // when the grid metronome is disabled.
    if (deck->consumeMetronomeTick())
        state.clickCursor = 0;

    // The beat grid only advances when the metronome is enabled and a tempo is
    // known. When it isn't, we still render any in-flight one-shot tap click
    // below, but don't keep a stale beat time around to re-sync against.
    const bool gridActive = deck->isMetronomeEnabled() && deck->getBPM() > 0.0f;

    double timePerBeat = 0.0, offsetTime = 0.0, currentInputTime = 0.0, s = 1.0;
    if (gridActive) {
        timePerBeat = 60.0 / (double)deck->getBPM();
        offsetTime = (double)deck->getBeatOffset() / (double)sampleRate;
        currentInputTime = deck->getCurrentInputTime();
        s = deck->getSpeed();

        // Re-sync nextBeatTime if it's way off or uninitialized
        if (state.nextBeatTime < 0 || std::abs(currentInputTime - state.nextBeatTime) > 2.0 * timePerBeat) {
            int64_t k = (int64_t)ceil((currentInputTime - offsetTime) / timePerBeat - 0.0001);
            state.nextBeatTime = offsetTime + (double)k * timePerBeat;
        }
    } else {
        state.nextBeatTime = -1.0;
    }

    for (size_t i = 0; i < framesPerBuffer; ++i) {
        if (gridActive) {
            // Check if currentInputTime crossed nextBeatTime, using a local
            // input time approximation for this buffer.
            double localInputTime = currentInputTime + (double)i / (double)sampleRate * s;

            if (localInputTime >= state.nextBeatTime) {
                state.clickCursor = 0;
                state.nextBeatTime += timePerBeat;
                // Catch up if needed
                while (localInputTime >= state.nextBeatTime) {
                    state.nextBeatTime += timePerBeat;
                }
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