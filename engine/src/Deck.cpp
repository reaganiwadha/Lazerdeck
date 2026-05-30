#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "Deck.hpp"
#include <cmath>
#include <samplerate.h>
#include "Clock.hpp"
#include "Logger.hpp"

// SINC quality used when resampling source files to the engine rate at decode
// time. MEDIUM is realtime-friendly while loading; bump to SRC_SINC_BEST_QUALITY
// for the highest quality at the cost of more CPU during load.
static constexpr int kResampleQuality = SRC_SINC_MEDIUM_QUALITY;

Deck::Deck(int sr) : currentFrame(0), visualFrame(0.0), playing(false), loading(false), framesAvailable(0), bpm(0.0f), beatOffset(0.0f), analyzing(false), metronomeEnabled(false), sampleRate(sr) {
    // Initialize RubberBand
    // Using OptionProcessRealTime for dynamic speed changes
    RubberBand::RubberBandStretcher::Options options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
    stretcher = new RubberBand::RubberBandStretcher(sampleRate, 2, options);
    stretcher->setMaxProcessSize(8192);

    // Reserve scratch buffers
    size_t reserveSize = 16384; 
    scratchIn[0].reserve(reserveSize);
    scratchIn[1].reserve(reserveSize);
    scratchOut[0].resize(reserveSize); 
    scratchOut[1].resize(reserveSize);
}

Deck::~Deck() {
    analysisCancel.store(true); // Bail the analysis loop fast, skip final getBpm()
    analyzing.store(false);     // Signal stop
    if (loaderThread.joinable()) loaderThread.join();
    if (analysisThread.joinable()) analysisThread.join();
    delete stretcher;
}

bool Deck::load(const std::string& filepath, AnalysisDB* db,
                double resumeSeconds, bool resumePlaying) {
    analysisCancel.store(true); // Bail any in-flight analysis so we don't block here
    analyzing.store(false);
    if (loaderThread.joinable()) loaderThread.join();
    if (analysisThread.joinable()) analysisThread.join();

    playing.store(false);
    currentFrame.store(0);
    framesAvailable.store(0);
    loading.store(true);

    // Clear old buffer data immediately
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        buffer = AudioBuffer(2, sampleRate, 0, {});
    }

    // Drop the old waveform summary so the UI shows nothing until the new
    // track starts streaming in, and reset the envelope follower so the first
    // bins of the new track aren't compared against the previous track.
    {
        std::lock_guard<std::mutex> lock(waveMutex);
        waveSummary.clear();
    }
    _waveEnvSlow = 0.0f;
    _wavePrevRms = 0.0f;

    bpm.store(0.0f);
    beatOffset.store(0.0f);
    bpmManual.store(false);

    // Reset loop and sync state
    loopActive.store(false);
    loopStart.store(0);
    loopEnd.store(0);
    recallStart.store(0);
    recallEnd.store(0);
    syncActive.store(false);
    syncSource.store(-1);
    
    // Reset stretcher and speed
    {
        std::lock_guard<std::mutex> lock(stretcherMutex);
        stretcher->reset();
        stretcher->setTimeRatio(1.0);
    }
    speed.store(1.0);
    currentProcessSpeed = 1.0;
    
    currentFilepath = filepath;
    currentFileHash = "";

    // Set before the loader starts so loaderWork applies it race-free.
    pendingResumeSeconds = resumeSeconds;
    pendingResumePlaying = resumePlaying;

    loaderThread = std::thread(&Deck::loaderWork, this, filepath, db);

    return true;
}

void Deck::saveAnalysis(AnalysisDB& db) {
    if (currentFilepath.empty()) return; 
    
    if (currentFileHash == "") {
        currentFileHash = AnalysisDB::computeHash(currentFilepath);
    }
    
    if (currentFileHash != "") {
        db.save(currentFileHash, bpm.load(), beatOffset.load());
        Logger::info("Saved analysis for " + currentFilepath);
    }
}

void Deck::reanalyze(AnalysisDB& db) {
    if (currentFilepath.empty()) return;
    if (loading.load()) return; // track still streaming in; ignore

    // Stop any analysis already in flight.
    analysisCancel.store(true);
    analyzing.store(false);
    if (analysisThread.joinable()) analysisThread.join();

    // Drop the cached entry so a fresh detection isn't short-circuited.
    if (currentFileHash == "") {
        currentFileHash = AnalysisDB::computeHash(currentFilepath);
    }
    if (currentFileHash != "") db.remove(currentFileHash);

    // Clear any manual override and the current value, then run detection again.
    bpmManual.store(false);
    bpm.store(0.0f);

    analysisCancel.store(false);
    analyzing.store(true);
    analysisThread = std::thread(&Deck::analyzeBPMWork, this, &db);
    Logger::info("Re-analyzing BPM for " + currentFilepath);
}

void Deck::addTrigger(int id, float beat) {
    std::lock_guard<std::mutex> lock(triggerMutex);
    // Remove existing if same ID
    triggers.erase(std::remove_if(triggers.begin(), triggers.end(), 
        [id](const DeckTrigger& t) { return t.id == id; }), triggers.end());
    
    triggers.push_back({id, beat, false, {}});
    Logger::info("Deck: Added trigger " + std::to_string(id) + " at beat " + std::to_string(beat));
}

void Deck::addTriggerAction(int id, const TriggerAction& action) {
    std::lock_guard<std::mutex> lock(triggerMutex);
    for (auto& t : triggers) {
        if (t.id == id) {
            t.actions.push_back(action);
            return;
        }
    }
}

std::vector<DeckTrigger>& Deck::getTriggers() {
    return triggers;
}

void Deck::resetTriggers() {
    std::lock_guard<std::mutex> lock(triggerMutex);
    for (auto& t : triggers) {
        t.fired = false;
    }
}

void Deck::analyzeBPMWork(AnalysisDB* db) {
    // BTrack consumes one hop (512 samples) of mono audio per call and reports
    // when a beat falls in that hop. We mix the stereo buffer to mono and feed
    // it hop by hop, recording the source-frame position of each detected beat.
    const int hopSize = 512;
    const int frameSize = 1024;
    BTrack btrack(hopSize, frameSize, sampleRate);

    std::vector<double> hop(hopSize);
    std::vector<uint64_t> beatFrames; // source-frame positions of detected beats

    uint64_t processedFrames = 0;
    const uint64_t maxFrames = (uint64_t)sampleRate * 300; // analyze first 5 minutes
    Logger::info("Starting BPM analysis (BTrack)...");

    while (analyzing.load()) {
        if (analysisCancel.load()) { analyzing.store(false); return; }

        uint64_t avail = framesAvailable.load();
        bool isLoaded = !loading.load();

        // BTrack needs a full hop before it can process; wait for the loader
        // to stream more in, or stop once the whole track has arrived.
        if (processedFrames + (uint64_t)hopSize > avail) {
            if (isLoaded) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (processedFrames + (uint64_t)hopSize > buffer.frameCount) break;
            const unsigned int ch = buffer.channels ? buffer.channels : 1;
            const float* src = buffer.frame(processedFrames);
            for (int i = 0; i < hopSize; ++i) {
                float sum = 0.0f;
                for (unsigned int c = 0; c < ch; ++c) sum += src[i * ch + c];
                hop[i] = (double)(sum / (float)ch);
            }
        }

        btrack.processAudioFrame(hop.data());
        if (btrack.beatDueInCurrentFrame())
            beatFrames.push_back(processedFrames);

        processedFrames += (uint64_t)hopSize;

        if (processedFrames % (uint64_t)(sampleRate * 5) < (uint64_t)hopSize) {
            float progress = (float)processedFrames /
                             (float)std::min(buffer.frameCount, maxFrames) * 100.0f;
            Logger::info("BPM analysis progress: " + std::to_string((int)progress) + "%");
        }

        if (processedFrames >= maxFrames) break;
    }

    // Cancelled mid-stream (new load or manual override) — don't touch bpm/DB.
    if (analysisCancel.load()) { analyzing.store(false); return; }

    // Derive BPM from the median inter-beat interval (robust to the occasional
    // spurious or dropped beat) and the grid phase (beatOffset, in frames) from
    // the circular mean of the beat positions modulo one beat period — circular
    // so beats straddling the 0 / framesPerBeat wrap don't bias the result.
    float resultBpm = 0.0f;
    float resultOffset = 0.0f;

    if (beatFrames.size() >= 4) {
        std::vector<double> ibis;
        ibis.reserve(beatFrames.size() - 1);
        for (size_t i = 1; i < beatFrames.size(); ++i)
            ibis.push_back((double)(beatFrames[i] - beatFrames[i - 1]));
        std::sort(ibis.begin(), ibis.end());
        const double medianIBI = ibis[ibis.size() / 2];

        if (medianIBI > 0.0) {
            resultBpm = (float)(60.0 * (double)sampleRate / medianIBI);
            const double framesPerBeat = medianIBI;

            double sumSin = 0.0, sumCos = 0.0;
            for (uint64_t bf : beatFrames) {
                const double theta = 2.0 * M_PI * ((double)bf / framesPerBeat);
                sumSin += std::sin(theta);
                sumCos += std::cos(theta);
            }
            double meanTheta = std::atan2(sumSin, sumCos);
            if (meanTheta < 0.0) meanTheta += 2.0 * M_PI;
            resultOffset = (float)(meanTheta / (2.0 * M_PI) * framesPerBeat);
        }
    } else {
        // Too few beats to trust the phase; fall back to BTrack's running tempo.
        resultBpm = (float)btrack.getCurrentTempoEstimate();
    }

    if (bpmManual.load() || analysisCancel.load()) {
        Logger::info("BPM analysis discarded (manual override or cancelled)");
    } else if (resultBpm > 0.0f) {
        bpm.store(resultBpm);
        beatOffset.store(resultOffset);
        Logger::info("BPM Detected: " + std::to_string(resultBpm) +
                     ", Beat offset: " + std::to_string(resultOffset) + " frames (from " +
                     std::to_string(beatFrames.size()) + " beats)");
        // Persist so future loads hit the DB instead of re-analyzing.
        if (db && currentFileHash != "") {
            db->save(currentFileHash, resultBpm, resultOffset);
            Logger::info("Saved analysis to DB for " + currentFilepath);
        }
    } else {
        Logger::warn("BPM detection failed");
    }

    analyzing.store(false);
}

void Deck::loaderWork(std::string filepath, AnalysisDB* db) {
    std::string hash = AnalysisDB::computeHash(filepath);
    currentFileHash = hash;

    // Check DB for analysis early
    bool analysisFound = false;
    float dbBpm = 0.0f, dbOffset = 0.0f;
    if (db && currentFileHash != "") {
        if (db->get(currentFileHash, dbBpm, dbOffset)) {
            bpm.store(dbBpm);
            beatOffset.store(dbOffset);
            Logger::info("Loaded analysis from DB: BPM " + std::to_string(dbBpm) + ", Offset " + std::to_string(dbOffset));
            analysisFound = true;
        }
    }

    // Decode at the file's native sample rate (force f32 + stereo only), then
    // resample to the engine rate with libsamplerate's SINC converter. This
    // replaces miniaudio's low-quality linear resampler, which aliased badly
    // when the source rate differed from the engine rate.
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init_default();
    config.format   = ma_format_f32;
    config.channels = 2; // Force stereo (leave sampleRate unset = decode native)

    ma_result result = ma_decoder_init_file(filepath.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        Logger::error("Failed to open file: " + filepath);
        loading.store(false);
        return;
    }

    const int nativeRate = (int)decoder.outputSampleRate;
    ma_uint64 nativeTotal = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &nativeTotal);

    const bool   needResample = (nativeRate > 0 && nativeRate != sampleRate);
    const double ratio        = needResample ? (double)sampleRate / (double)nativeRate : 1.0;

    // Engine-rate frame count. When resampling, over-allocate a small margin so
    // SINC output (which can round a frame or two past ceil) always fits. The
    // buffer is sized ONCE here and never reallocated while the audio thread may
    // be reading it — framesAvailable gates how far playback can advance.
    const uint64_t bufferFrames = needResample
        ? (uint64_t)std::ceil((double)nativeTotal * ratio) + 256
        : (uint64_t)nativeTotal;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        buffer = AudioBuffer(2, sampleRate, 0, {}); // Empty data initially
        buffer.resize(bufferFrames);                // Allocate vector once
    }

    fftEnergy.initialize(&buffer);

    // Resume near the previous source-time position (e.g. after a sample-rate
    // change re-decodes this track). process() underrun-waits if the target
    // frame hasn't streamed in yet, so playback catches up as frames arrive.
    if (pendingResumeSeconds >= 0.0) {
        uint64_t target = (uint64_t)(pendingResumeSeconds * (double)sampleRate);
        if (bufferFrames > 0 && target >= bufferFrames) target = bufferFrames - 1;
        currentFrame.store(target);
        currentInputTime = (double)target / (double)sampleRate;
        if (pendingResumePlaying) playing.store(true);
        pendingResumeSeconds = -1.0;
    }

    const uint64_t chunkSize = 4096;
    uint64_t framesWrittenTotal = 0; // engine-rate frames committed to the buffer

    // Publish freshly written engine-rate frames: cache their FFT energy (needed
    // for waveform colour/transients), advance framesAvailable so playback can
    // reach them, then summarize complete waveform bins.
    auto commit = [&](uint64_t newTotal) {
        fftEnergy.analyzeChunk(framesWrittenTotal, newTotal - framesWrittenTotal);
        framesWrittenTotal = newTotal;
        framesAvailable.store(framesWrittenTotal);
        summarizeUpTo(framesWrittenTotal, false);
    };

    if (!needResample) {
        // Fast path: file already at the engine rate — decode straight in.
        while (framesWrittenTotal < bufferFrames) {
            float* pWrite = buffer.frame(framesWrittenTotal);
            ma_uint64 got = 0;
            result = ma_decoder_read_pcm_frames(&decoder, pWrite, chunkSize, &got);
            if (got == 0) break;
            commit(framesWrittenTotal + got);
            if (result != MA_SUCCESS) break;
        }
    } else {
        Logger::info("Resampling " + std::to_string(nativeRate) + " Hz -> " +
                     std::to_string(sampleRate) + " Hz (SINC)");
        int srcErr = 0;
        SRC_STATE* src = src_new(kResampleQuality, 2, &srcErr);
        if (!src) Logger::error("src_new failed: " + std::string(src_strerror(srcErr)));

        std::vector<float> inBuf(chunkSize * 2);
        bool eof = false;
        while (src && !eof && framesWrittenTotal < bufferFrames) {
            ma_uint64 got = 0;
            result = ma_decoder_read_pcm_frames(&decoder, inBuf.data(), chunkSize, &got);
            eof = (got == 0) || (result != MA_SUCCESS);

            SRC_DATA data;
            data.data_in      = inBuf.data();
            data.input_frames  = (long)got;
            data.data_out      = buffer.frame(framesWrittenTotal);
            data.output_frames = (long)(bufferFrames - framesWrittenTotal);
            data.src_ratio     = ratio;
            data.end_of_input  = eof ? 1 : 0;

            int e = src_process(src, &data);
            if (e) {
                Logger::error("src_process failed: " + std::string(src_strerror(e)));
                break;
            }
            if (data.output_frames_gen > 0)
                commit(framesWrittenTotal + (uint64_t)data.output_frames_gen);
            // The output region (rest of the track) is always large enough to
            // consume a whole input chunk, so input is fully drained each call.
            if (eof && data.output_frames_gen == 0) break; // SINC tail drained
        }
        if (src) src_delete(src);
    }

    // Flush the trailing partial bin once all frames are in, then normalize
    // transients across the whole track so the visuals are track-relative.
    summarizeUpTo(framesWrittenTotal, true);
    normalizeWaveTransients();

    ma_decoder_uninit(&decoder);

    loading.store(false);
    Logger::info("Loaded " + std::to_string(framesWrittenTotal) + " frames from " + filepath);

    if (!analysisFound) {
        analysisCancel.store(false);
        analyzing.store(true);
        analysisThread = std::thread(&Deck::analyzeBPMWork, this, db);
    }
}

void Deck::process(float* outputBuffer, unsigned long framesPerBuffer) {
    if (!playing.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(stretcherMutex);

    // Update speed if changed
    double s = speed.load();
    if (std::abs(s - currentProcessSpeed) > 0.0001) {
        if (s < 0.01) s = 0.01; // Protect against 0
        stretcher->setTimeRatio(1.0 / s);
        currentProcessSpeed = s;
    }

    size_t framesRetrievedTotal = 0;
    float* outPtrs[2] = { scratchOut[0].data(), scratchOut[1].data() };

    while (framesRetrievedTotal < framesPerBuffer) {
        int avail = stretcher->available();
        
        // If we have enough output available, retrieve it
        if (avail > 0 && (size_t)avail >= (framesPerBuffer - framesRetrievedTotal)) {
             size_t toRetrieve = framesPerBuffer - framesRetrievedTotal;
             size_t got = stretcher->retrieve(outPtrs, toRetrieve);
             
             // Interleave to output
             for (size_t i = 0; i < got; ++i) {
                 outputBuffer[(framesRetrievedTotal + i) * 2 + 0] += outPtrs[0][i];
                 outputBuffer[(framesRetrievedTotal + i) * 2 + 1] += outPtrs[1][i];
             }
             
             currentInputTime += (double)got / (double)sampleRate * s;
             framesRetrievedTotal += got;
             break;
        }

        // Retrieve what's available
        if (avail > 0) {
            size_t got = stretcher->retrieve(outPtrs, avail);

            for (size_t i = 0; i < got; ++i) {
                outputBuffer[(framesRetrievedTotal + i) * 2 + 0] += outPtrs[0][i];
                outputBuffer[(framesRetrievedTotal + i) * 2 + 1] += outPtrs[1][i];
            }
            
            currentInputTime += (double)got / (double)sampleRate * s;
            framesRetrievedTotal += got;
        }

        // We need more output, so we need to provide input
        size_t needed = stretcher->getSamplesRequired();
        if (needed == 0) needed = 1024;
        
        uint64_t fr = currentFrame.load(std::memory_order_relaxed);
        uint64_t frAvail = framesAvailable.load(std::memory_order_relaxed);
        bool lActive = loopActive.load();
        uint64_t lStart = loopStart.load();
        uint64_t lEnd = loopEnd.load();
        bool final = false;

        size_t actualInput = needed;

        if (lActive) {
            if (fr >= lEnd) {
                currentFrame.store(lStart);
                currentInputTime = (double)lStart / (double)sampleRate;
                continue;
            }
            if (fr + needed > lEnd) {
                actualInput = lEnd - fr;
            }
        } else if (fr + needed >= frAvail) {
            if (!loading.load()) {
                actualInput = (frAvail > fr) ? (frAvail - fr) : 0;
                final = true;
            } else {
                 actualInput = (frAvail > fr) ? (frAvail - fr) : 0;
                 if (actualInput == 0) {
                     break; 
                 }
            }
        }
        
        if (actualInput > 0) {
            // Resize scratchIn if needed
            if (scratchIn[0].capacity() < actualInput) {
                scratchIn[0].reserve(actualInput + 1024);
                scratchIn[1].reserve(actualInput + 1024);
            }
            scratchIn[0].resize(actualInput);
            scratchIn[1].resize(actualInput);

            // De-interleave ONLY (no metronome here, we want it un-stretched)
            for (size_t i = 0; i < actualInput; ++i) {
                scratchIn[0][i] = buffer.sample(fr + i, 0);
                scratchIn[1][i] = buffer.sample(fr + i, 1);
            }

            const float* inPtrs[2] = { scratchIn[0].data(), scratchIn[1].data() };
            stretcher->process(inPtrs, actualInput, final);
            
            uint64_t nextFr = fr + actualInput;
            if (lActive && nextFr >= lEnd) {
                nextFr = lStart;
                currentInputTime = (double)lStart / (double)sampleRate;
            }
            currentFrame.store(nextFr, std::memory_order_relaxed);
        } else {
            if (final) {
                stretcher->process(nullptr, 0, true);
                if (stretcher->available() <= 0) {
                    playing.store(false);
                    break;
                }
            } else {
                break; // Underrun
            }
        }
    }
}

void Deck::play() {
    if (framesAvailable.load() > 0 || loading.load()) {
        playing.store(true);
    }
}

void Deck::pause() {
    playing.store(false);
}

void Deck::togglePlayback() {
    if (playing.load()) {
        playing.store(false);
    } else {
        if (framesAvailable.load() > 0 || loading.load()) {
            playing.store(true);
        }
    }
}

void Deck::seek(int64_t frameOffset) {
    uint64_t current = currentFrame.load();
    int64_t target = (int64_t)current + frameOffset;
    uint64_t avail = framesAvailable.load();
    
    if (target < 0) target = 0;
    if (target >= (int64_t)avail) target = avail > 0 ? avail - 1 : 0;
    
    currentFrame.store((uint64_t)target);
    currentInputTime = (double)target / (double)sampleRate;
    
    // Reset RubberBand on seek to avoid artifacts/delay confusion
    std::lock_guard<std::mutex> lock(stretcherMutex);
    stretcher->reset();
    stretcher->setTimeRatio(1.0 / speed.load());
}

void Deck::setFrame(uint64_t frame) {
    uint64_t avail = framesAvailable.load();
    if (frame >= avail) frame = avail > 0 ? avail - 1 : 0;
    currentFrame.store(frame);
    currentInputTime = (double)frame / (double)sampleRate;
    
    std::lock_guard<std::mutex> lock(stretcherMutex);
    stretcher->reset();
    stretcher->setTimeRatio(1.0 / speed.load());
}

uint64_t Deck::getCurrentFrame() const {
    return currentFrame.load(std::memory_order_relaxed);
}

const AudioBuffer& Deck::getBuffer() const {
    return buffer;
}

const FFTEnergy& Deck::getFFTEnergy() const {
    return fftEnergy;
}

bool Deck::isPlaying() const {
    return playing.load(std::memory_order_relaxed);
}

bool Deck::isLoading() const {
    return loading.load(std::memory_order_relaxed);
}

uint64_t Deck::getFramesAvailable() const {
    return framesAvailable.load(std::memory_order_relaxed);
}

namespace {
// MiniMeters-style spectral palette: bass -> red, mids -> warm white, highs ->
// cyan/blue. `t` is the spectral position in [0,1] (0 = bass, 1 = treble).
void spectralColor(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    struct Stop { float t, r, g, b; };
    static const Stop stops[] = {
        {0.00f, 235.f,  40.f,  25.f},  // deep red
        {0.28f, 255.f, 120.f,  30.f},  // orange
        {0.48f, 255.f, 235.f, 220.f},  // warm white
        {0.70f,  70.f, 220.f, 230.f},  // cyan
        {1.00f,  45.f, 110.f, 255.f},  // blue
    };
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    const int n = (int)(sizeof(stops) / sizeof(stops[0]));
    for (int i = 1; i < n; ++i) {
        if (t <= stops[i].t) {
            const Stop& a = stops[i - 1];
            const Stop& c = stops[i];
            float f = (c.t > a.t) ? (t - a.t) / (c.t - a.t) : 0.f;
            r = (uint8_t)(a.r + (c.r - a.r) * f);
            g = (uint8_t)(a.g + (c.g - a.g) * f);
            b = (uint8_t)(a.b + (c.b - a.b) * f);
            return;
        }
    }
    r = (uint8_t)stops[n - 1].r;
    g = (uint8_t)stops[n - 1].g;
    b = (uint8_t)stops[n - 1].b;
}
}  // namespace

WaveBin Deck::makeWaveBin(uint64_t startFrame, uint32_t frameCount) const {
    float mn = 1.0f, mx = -1.0f;
    double sumSq = 0.0;
    // Cap the scan per bin so wide bins stay cheap.
    uint32_t stride = frameCount > 256 ? frameCount / 256 : 1;
    uint32_t count = 0;
    for (uint64_t f = startFrame; f < startFrame + frameCount; f += stride) {
        float s = buffer.sample(f, 0);
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        sumSq += (double)s * s;
        ++count;
    }
    if (mn > mx) { mn = 0.0f; mx = 0.0f; }
    float rms = (count > 0) ? std::sqrt((float)(sumSq / count)) : 0.0f;

    uint8_t r, g, b;
    fftEnergy.getColorAtFrame(startFrame + frameCount / 2, r, g, b);
    uint32_t rgba = (0xFFu << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);

    return WaveBin{ mn, mx, rms, 0.0f, rgba }; // transient filled by summarizeUpTo
}

void Deck::summarizeUpTo(uint64_t avail, bool finalChunk) {
    // Envelope follower coefficients (per-bin, ~5.8 ms at 44.1 kHz / 256-frame bins).
    constexpr float kSlowAttack  = 0.02f;
    constexpr float kSlowRelease = 0.001f;

    auto addBin = [&](uint64_t start, uint32_t frames) {
        WaveBin bin = makeWaveBin(start, frames);

        // Slow envelope follower — tracks average energy, making sudden rises
        // detectable as transients.
        float coeff = (bin.rms > _waveEnvSlow) ? kSlowAttack : kSlowRelease;
        _waveEnvSlow += coeff * (bin.rms - _waveEnvSlow);

        // Combine envelope-follower method (energy above slow average) and
        // log-energy method (magnitude of sudden increase vs. previous bin).
        auto logE = [](float r) { return std::log(r + 1e-8f); };
        float envTransient  = std::max(0.0f, bin.rms - _waveEnvSlow);
        float logTransient  = std::max(0.0f, logE(bin.rms) - logE(_wavePrevRms));
        bin.transient = std::max(envTransient, logTransient);

        _wavePrevRms = bin.rms;
        waveSummary.push_back(bin);
    };

    std::lock_guard<std::mutex> lock(waveMutex);
    uint64_t startFrame = (uint64_t)waveSummary.size() * kWaveBinFrames;
    while (startFrame + kWaveBinFrames <= avail) {
        addBin(startFrame, kWaveBinFrames);
        startFrame += kWaveBinFrames;
    }
    if (finalChunk && startFrame < avail) {
        addBin(startFrame, (uint32_t)(avail - startFrame));
    }
}

void Deck::normalizeWaveTransients() {
    std::lock_guard<std::mutex> lock(waveMutex);
    if (waveSummary.empty()) return;
    float maxT = 1e-6f;
    for (const auto& b : waveSummary) maxT = std::max(maxT, b.transient);
    for (auto& b : waveSummary) {
        b.transient /= maxT;
        b.transient = std::sqrt(b.transient); // sqrt spreads medium transients
    }
}

uint64_t Deck::getWaveBinCount() const {
    std::lock_guard<std::mutex> lock(waveMutex);
    return waveSummary.size();
}

uint32_t Deck::copyWaveBins(uint64_t start, uint32_t count,
                            float* outMinMax, uint32_t* outRgba) const {
    std::lock_guard<std::mutex> lock(waveMutex);
    if (start >= waveSummary.size()) return 0;
    uint64_t end = start + count;
    if (end > waveSummary.size()) end = waveSummary.size();
    uint32_t n = (uint32_t)(end - start);
    for (uint32_t i = 0; i < n; ++i) {
        const WaveBin& bin = waveSummary[start + i];
        if (outMinMax) {
            outMinMax[i * 4]     = bin.mn;
            outMinMax[i * 4 + 1] = bin.mx;
            outMinMax[i * 4 + 2] = bin.rms;
            outMinMax[i * 4 + 3] = bin.transient;
        }
        if (outRgba) outRgba[i] = bin.rgba;
    }
    return n;
}

void Deck::setSpeed(double s) {
    if (s < 0.001) s = 0.001;
    if (s > 4.0) s = 4.0;
    speed.store(s);
}

double Deck::getSpeed() const {
    return speed.load();
}

void Deck::increaseSpeed() {
    float b = bpm.load();
    if (b > 0.0f) {
        setSpeed(speed.load() + (1.0 / b));
    } else {
        setSpeed(speed.load() + 0.01);
    }
}

void Deck::decreaseSpeed() {
    float b = bpm.load();
    if (b > 0.0f) {
        setSpeed(speed.load() - (1.0 / b));
    } else {
        setSpeed(speed.load() - 0.01);
    }
}

float Deck::getEffectiveBPM() const {
    return bpm.load() * (float)speed.load();
}

double Deck::getVisualFrame() const {
    return visualFrame.load();
}

void Deck::updateSampleRate(int newSampleRate) {
    if (sampleRate == newSampleRate) return;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        sampleRate = newSampleRate;
    }
    
    {
        std::lock_guard<std::mutex> lock(stretcherMutex);
        if (stretcher) delete stretcher;
        RubberBand::RubberBandStretcher::Options options = RubberBand::RubberBandStretcher::OptionProcessRealTime;
        stretcher = new RubberBand::RubberBandStretcher(sampleRate, 2, options);
        stretcher->setMaxProcessSize(8192);
        stretcher->setTimeRatio(1.0 / speed.load());
    }

    Logger::info("Deck sample rate updated to " + std::to_string(newSampleRate) + " Hz");
}

void Deck::updateVisualFrame() {
    if (playing.load()) {
        uint64_t now = lzr::nowMs();
        if (lastVisualUpdateTime == 0) {
            lastVisualUpdateTime = now;
            lastVisualFrame = static_cast<double>(currentFrame.load());
            visualFrame.store(lastVisualFrame);
        } else {
            double dt = (now - lastVisualUpdateTime) / 1000.0; // seconds
            uint64_t current = currentFrame.load();
            
            // Interpolate between last known audio frame and current
            double expectedAdvance = dt * sampleRate * speed.load();
            double interpolated = lastVisualFrame + expectedAdvance;
            
            // Clamp to actual current frame (don't go past what audio has processed)
            if (interpolated > static_cast<double>(current)) {
                interpolated = static_cast<double>(current);
            }
            
            visualFrame.store(interpolated);
            lastVisualUpdateTime = now;
            lastVisualFrame = static_cast<double>(current);
        }
    } else {
        lastVisualUpdateTime = 0;
        visualFrame.store(static_cast<double>(currentFrame.load()));
    }
}

void Deck::setLoopStart() {
    uint64_t current = currentFrame.load();
    float b = bpm.load();
    if (b > 0.0f) {
        float offset = beatOffset.load();
        double framesPerBeat = (double)sampleRate * 60.0 / (double)b;
        int64_t beatIndex = (int64_t)std::round(((double)current - offset) / framesPerBeat);
        uint64_t snapped = (uint64_t)std::max(0.0, (double)beatIndex * framesPerBeat + offset);
        loopStart.store(snapped);
        Logger::info("Loop Start set (snapped to beat " + std::to_string(beatIndex + 1) + ")");
    } else {
        loopStart.store(current);
        Logger::info("Loop Start set (no BPM, unsnapped)");
    }
}

void Deck::setLoopEnd() {
    uint64_t current = currentFrame.load();
    uint64_t start = loopStart.load();
    float b = bpm.load();
    uint64_t finalEnd = current;

    if (b > 0.0f) {
        float offset = beatOffset.load();
        double framesPerBeat = (double)sampleRate * 60.0 / (double)b;
        int64_t beatIndex = (int64_t)std::round(((double)current - offset) / framesPerBeat);
        finalEnd = (uint64_t)std::max(0.0, (double)beatIndex * framesPerBeat + offset);
        
        // Ensure loop has length and end is after start
        if (finalEnd <= start) {
            // Force it to at least the next beat boundary
            beatIndex = (int64_t)std::floor(((double)current - offset) / framesPerBeat) + 1;
            finalEnd = (uint64_t)std::max(0.0, (double)beatIndex * framesPerBeat + offset);
            
            // If still not enough, fallback to 1 beat duration
            if (finalEnd <= start) {
                finalEnd = start + (uint64_t)framesPerBeat;
            }
        }
        Logger::info("Loop End set (snapped to beat " + std::to_string(beatIndex + 1) + ")");
    } else {
        if (finalEnd <= start) {
            Logger::warn("Loop End must be after Loop Start");
            return;
        }
        Logger::info("Loop End set (no BPM, unsnapped)");
    }

    loopEnd.store(finalEnd);
    loopActive.store(true);
    Logger::info("Loop Active: " + std::to_string(start) + " -> " + std::to_string(finalEnd));
}

void Deck::setLoopRange(uint64_t start, uint64_t end) {
    if (end <= start) {
        Logger::warn("Loop End must be after Loop Start");
        return;
    }
    loopStart.store(start);
    loopEnd.store(end);
    loopActive.store(true);
    Logger::info("Loop Active (Direct): " + std::to_string(start) + " -> " + std::to_string(end));
}

void Deck::exitLoop() {
    uint64_t start = loopStart.load();
    uint64_t end = loopEnd.load();
    if (start > 0) {
        recallStart.store(start);
        recallEnd.store(end);
    }
    loopActive.store(false);
    Logger::info("Loop Exited (Recalled stored: " + std::to_string(start) + ")");
}

void Deck::clearLoop() {
    uint64_t start = loopStart.load();
    uint64_t end = loopEnd.load();
    if (start > 0) {
        recallStart.store(start);
        recallEnd.store(end);
    }
    loopActive.store(false);
    loopStart.store(0);
    loopEnd.store(0);
    std::lock_guard<std::mutex> lock(triggerMutex);
    triggers.clear();
    Logger::info("Loop and Cue markers cleared (Recall stored: " + std::to_string(start) + ")");
}

