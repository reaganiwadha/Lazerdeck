#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "Deck.hpp"
#include <cmath>
#include "Logger.hpp"

Deck::Deck(int sr) : currentFrame(0), playing(false), loading(false), framesAvailable(0), bpm(0.0f), beatOffset(0.0f), analyzing(false), metronomeEnabled(false), sampleRate(sr) {
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
    analyzing.store(false); // Signal stop
    if (loaderThread.joinable()) loaderThread.join();
    if (analysisThread.joinable()) analysisThread.join();
    delete stretcher;
}

bool Deck::load(const std::string& filepath, AnalysisDB* db) {
    analyzing.store(false);
    if (loaderThread.joinable()) loaderThread.join();
    if (analysisThread.joinable()) analysisThread.join();

    playing.store(false);
    currentFrame.store(0);
    framesAvailable.store(0);
    loading.store(true);

    bpm.store(0.0f);
    beatOffset.store(0.0f);
    
    // Reset stretcher and speed
    {
        std::lock_guard<std::mutex> lock(stretcherMutex);
        stretcher->reset();
        stretcher->setTimeRatio(1.0);
    }
    speed.store(1.0);
    currentProcessSpeed = 1.0;
    
    currentFilepath = filepath;
    currentFileHash = 0;

    loaderThread = std::thread(&Deck::loaderWork, this, filepath, db);

    return true;
}

void Deck::saveAnalysis(AnalysisDB& db) {
    if (currentFilepath.empty()) return; 
    
    if (currentFileHash == 0) {
        currentFileHash = AnalysisDB::computeHash(currentFilepath);
    }
    
    if (currentFileHash != 0) {
        db.save(currentFileHash, bpm.load(), beatOffset.load());
        Logger::info("Saved analysis for " + currentFilepath);
    }
}

void Deck::analyzeBPMWork() {
    soundtouch::BPMDetect bpmDetector(2, sampleRate);

    uint64_t processedFrames = 0;
    const uint64_t chunkSize = 2048;
    const uint64_t maxFrames = (uint64_t)sampleRate * 300;
    Logger::info("Starting BPM analysis...");

    while (analyzing.load()) {
        uint64_t avail = framesAvailable.load();
        bool isLoaded = !loading.load();
        uint64_t framesToProcess = chunkSize;

        if (processedFrames + chunkSize > avail) {
            if (isLoaded) {
                if (processedFrames < avail) {
                    framesToProcess = avail - processedFrames;
                } else {
                    break;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        std::lock_guard<std::mutex> lock(bufferMutex);
        if (processedFrames >= buffer.frameCount) {
            break;
        }
        
        // Safety check
        if (processedFrames + framesToProcess > buffer.frameCount) {
            framesToProcess = buffer.frameCount > processedFrames ? buffer.frameCount - processedFrames : 0;
        }
        
        if (framesToProcess == 0) break;

        const float* src = buffer.frame(processedFrames);

        try {
             bpmDetector.inputSamples(src, framesToProcess);
        } catch (...) {
             break;
        }

        processedFrames += framesToProcess;

        if (processedFrames % (sampleRate * 5) < chunkSize) {
            float progress = (float)processedFrames / (float)std::min(buffer.frameCount, maxFrames) * 100.0f;
            // Logger doesn't support \r well, but we'll just log it occasionally
            if ((int)progress % 10 == 0) {
                Logger::info("BPM analysis progress: " + std::to_string((int)progress) + "%");
            }
        }

        if (processedFrames >= maxFrames) {
            break;
        }
    }

    Logger::info("Analyzing BPM from " + std::to_string(processedFrames) + " frames...");

    float result = bpmDetector.getBpm();
    if (result > 0.0f) {
        bpm.store(result);
        Logger::info("BPM Detected: " + std::to_string(result));
    } else {
        Logger::warn("BPM detection failed");
    }

    analyzing.store(false);
}

void Deck::loaderWork(std::string filepath, AnalysisDB* db) {
    ma_decoder decoder;
    ma_result result = ma_decoder_init_file(filepath.c_str(), NULL, &decoder);
    if (result != MA_SUCCESS) {
        Logger::error("Failed to open file: " + filepath);
        loading.store(false);
        return;
    }

    uint64_t hash = AnalysisDB::computeHash(filepath);
    currentFileHash = hash;

    ma_uint64 totalFrames;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS) {
        totalFrames = 0; 
    }
    
    ma_decoder_uninit(&decoder);
    
    ma_decoder_config config = ma_decoder_config_init_default();
    config.format = ma_format_f32;
    config.channels = 2; // Force stereo
    config.sampleRate = sampleRate; // Dynamic resampling
    
    result = ma_decoder_init_file(filepath.c_str(), &config, &decoder);
    if (result != MA_SUCCESS) {
        Logger::error("Failed to open file (2nd try): " + filepath);
        loading.store(false);
        return;
    }
    
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    
    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        buffer = AudioBuffer(2, sampleRate, 0, {}); // Empty data initially
        buffer.resize(totalFrames); // Allocate vector
    }
    
    fftEnergy.initialize(&buffer);
    
    uint64_t framesReadTotal = 0;
    uint64_t chunkSize = 4096;
    
    while (framesReadTotal < totalFrames) {
        float* pWrite = buffer.frame(framesReadTotal);
        ma_uint64 framesReadThisIter;
        
        result = ma_decoder_read_pcm_frames(&decoder, pWrite, chunkSize, &framesReadThisIter);
        
        if (framesReadThisIter == 0) break;
        
        framesReadTotal += framesReadThisIter;
        
        fftEnergy.analyzeChunk(framesReadTotal - framesReadThisIter, framesReadThisIter);
        
        framesAvailable.store(framesReadTotal);
        
        if (result != MA_SUCCESS) break; 
    }
    
    ma_decoder_uninit(&decoder);

    loading.store(false);
    Logger::info("Loaded " + std::to_string(framesReadTotal) + " frames from " + filepath);

    // Check DB for analysis
    bool analysisFound = false;
    if (db && currentFileHash != 0) {
        float dbBpm, dbOffset;
        if (db->get(currentFileHash, dbBpm, dbOffset)) {
            bpm.store(dbBpm);
            beatOffset.store(dbOffset);
            Logger::info("Loaded analysis from DB: BPM " + std::to_string(dbBpm) + ", Offset " + std::to_string(dbOffset));
            analysisFound = true;
        }
    }

    if (!analysisFound) {
        analyzing.store(true);
        analysisThread = std::thread(&Deck::analyzeBPMWork, this);
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
        bool final = false;

        size_t actualInput = needed;
        if (fr + needed >= frAvail) {
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
            currentFrame.store(fr + actualInput, std::memory_order_relaxed);
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
    playing.store(true);
}

void Deck::pause() {
    playing.store(false);
}

void Deck::togglePlayback() {
    bool p = playing.load();
    playing.store(!p);
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

void Deck::setSpeed(double s) {
    if (s < 0.1) s = 0.1;
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
