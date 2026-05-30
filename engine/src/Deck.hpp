#pragma once

#include <string>
#include <atomic>
#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include "audio.hpp"
#include "fft.hpp"
#include "BTrack.h"
#include <rubberband/RubberBandStretcher.h>
#include "AnalysisDB.hpp"
#include "Trigger.hpp"

// One precomputed waveform bin. Built once during load; the UI reads these
// directly and never rescans the raw audio buffer per frame.
struct WaveBin {
    float    mn;         // min sample in the bin       [-1, 1]
    float    mx;         // max sample in the bin       [-1, 1]
    float    rms;        // RMS amplitude               [0, 1]
    float    transient;  // normalized transient strength [0, 1] after final pass
    uint32_t rgba;       // 0xAARRGGBB color from FFT energy
};

class Deck {
public:
    Deck(int sampleRate = 44100);
    ~Deck();

    bool load(const std::string& filepath, AnalysisDB* db = nullptr);
    void saveAnalysis(AnalysisDB& db);
    // Drops this track's cached analysis and runs BPM detection again.
    void reanalyze(AnalysisDB& db);

    // Triggers
    void addTrigger(int id, float beat);
    void addTriggerAction(int id, const TriggerAction& action);
    std::vector<DeckTrigger>& getTriggers();
    void resetTriggers();
    
    void process(float* outputBuffer, unsigned long framesPerBuffer);
    
    // VST methods moved to MixerChannel
    
    // Playback control
    void play();
    void pause();
    void togglePlayback();
    void seek(int64_t frameOffset);
    void setFrame(uint64_t frame);
    
    int getSampleRate() const { return sampleRate; }
    
    // Getters
    uint64_t getCurrentFrame() const;
    double getVisualFrame() const;
    const AudioBuffer& getBuffer() const;
    const FFTEnergy& getFFTEnergy() const;
    bool isPlaying() const;
    bool isLoading() const;
    uint64_t getFramesAvailable() const;
    std::string getCurrentFilepath() const { return currentFilepath; }

    float getBPM() const { return bpm.load(); }
    void setBPM(float b) { bpm.store(b); }

    // Manual BPM override: pins the value and stops auto-analysis from
    // clobbering it (see analyzeBPMWork). Cleared on the next load().
    void setBpmManual(float b) {
        bpm.store(b);
        bpmManual.store(true);
        analysisCancel.store(true);
        analyzing.store(false);
    }

    float getBeatOffset() const { return beatOffset.load(); }
    void setBeatOffset(float o) { beatOffset.store(o); }
    void nudgeBeatOffset(float delta) { beatOffset.store(beatOffset.load() + delta); }

    bool isAnalyzing() const { return analyzing.load(); }

    // --- Waveform summary (for UI rendering) ---
    // Number of source frames each WaveBin spans.
    static constexpr uint32_t kWaveBinFrames = 256;
    uint32_t getWaveBinFrames() const { return kWaveBinFrames; }
    // Total bins currently available (grows during load).
    uint64_t getWaveBinCount() const;
    // Copies up to `count` bins starting at `start` into the caller's buffers.
    // outMinMax receives 2 floats per bin (min, max); outRgba receives 1 per
    // bin. Either may be null. Returns the number of bins actually copied.
    uint32_t copyWaveBins(uint64_t start, uint32_t count,
                          float* outMinMax, uint32_t* outRgba) const;

        void toggleMetronome() { metronomeEnabled.store(!metronomeEnabled.load()); }
        void setMetronome(bool on) { metronomeEnabled.store(on); }

        bool isMetronomeEnabled() const { return metronomeEnabled.load(); }

        // One-shot metronome click, independent of the grid (used by the Tap
        // Tempo wizard so every tap is audible even when the grid metronome is
        // off). requestMetronomeTick() is called from the UI/command thread;
        // consumeMetronomeTick() is called once per buffer on the audio thread.
        void requestMetronomeTick() { metronomeTick.store(true); }
        bool consumeMetronomeTick() { return metronomeTick.exchange(false); }

    

        // Speed Control

    void setSpeed(double s);

    double getSpeed() const;
    
    void updateVisualFrame();
    void updateSampleRate(int newSampleRate);
    
    void increaseSpeed();

        void decreaseSpeed();

        float getEffectiveBPM() const;

    

    double getCurrentInputTime() const { return currentInputTime; }

    void setLoopStart();
    void setLoopEnd();
    void setLoopRange(uint64_t start, uint64_t end);
    void exitLoop();
    void clearLoop();
    bool isLoopActive() const { return loopActive.load(); }
    uint64_t getLoopStart() const { return loopStart.load(); }
    uint64_t getLoopEnd() const { return loopEnd.load(); }
    uint64_t getRecallStart() const { return recallStart.load(); }
    uint64_t getRecallEnd() const { return recallEnd.load(); }

    // Sync
    void setSync(bool active, int sourceIdx = -1) { 
        syncActive.store(active); 
        syncSource.store(sourceIdx);
    }
    bool isSyncActive() const { return syncActive.load(); }
    int getSyncSource() const { return syncSource.load(); }

    // Declarative Definition
    void beginDefinition();
    void endDefinition();
    void markVSTDefined(int index);
    void markTriggerDefined(int id);

    private:

        void loaderWork(std::string filepath, AnalysisDB* db);

        void analyzeBPMWork(AnalysisDB* db);

        // Appends complete WaveBins covering frames up to `framesAvailable`.
        // When `finalChunk` is set, also emits a trailing bin for the remainder.
        void summarizeUpTo(uint64_t framesAvailable, bool finalChunk);
        WaveBin makeWaveBin(uint64_t startFrame, uint32_t frameCount) const;
        // Scales all transients to [0,1] and applies sqrt for perceptual spread.
        // Called once after the loader has emitted the final bin.
        void normalizeWaveTransients();

        std::vector<WaveBin> waveSummary;
        mutable std::mutex   waveMutex;
        // Envelope follower state for transient detection — only touched on the
        // loader thread, no locking needed.
        float _waveEnvSlow  = 0.0f;
        float _wavePrevRms  = 0.0f;

    

        AudioBuffer buffer;

        std::atomic<uint64_t> currentFrame;

        std::atomic<double> visualFrame{0.0};
        uint64_t lastVisualUpdateTime = 0;
        double lastVisualFrame = 0.0;

        std::atomic<bool> playing;

        FFTEnergy fftEnergy;

        

        // Streaming / Loading

        std::atomic<bool> loading;

        std::atomic<uint64_t> framesAvailable;

        std::thread loaderThread;

        std::mutex bufferMutex; // Protects resizing if needed, though we try to pre-alloc

    

        // BPM / Analysis

        std::atomic<float> bpm{0.0f};

        std::atomic<float> beatOffset{0.0f};

        std::atomic<bool> analyzing{false};

        // Requests the running analysis thread bail out immediately (without
        // running the expensive final getBpm() or persisting a result). Set by
        // load()/setBpmManual()/~Deck so a new load doesn't block on a stale
        // analysis. Cleared right before a fresh analysis starts.
        std::atomic<bool> analysisCancel{false};

        // Set when the user overrides BPM by hand; blocks analyzeBPMWork from
        // overwriting it. Reset on load().
        std::atomic<bool> bpmManual{false};

        std::thread analysisThread;

    

        // Metronome state (enabled/disabled)

        std::atomic<bool> metronomeEnabled{false};

        // Set by requestMetronomeTick(), consumed once on the audio thread.
        std::atomic<bool> metronomeTick{false};

        

        // File Info

        std::string currentFilepath;
        std::string currentFileHash = "";

    

                // Time Stretching

    

                RubberBand::RubberBandStretcher* stretcher = nullptr;

    

                std::mutex stretcherMutex;

    

                std::atomic<double> speed{1.0};

    

                double currentProcessSpeed = 1.0;

    

                double currentInputTime = 0.0; // In seconds, original track time

    

                int sampleRate;

    

        

    

                // Looping

    

                std::atomic<bool> loopActive{false};

    

                std::atomic<uint64_t> loopStart{0};

    

                    std::atomic<uint64_t> loopEnd{0};

                    std::atomic<uint64_t> recallStart{0};

                    std::atomic<uint64_t> recallEnd{0};





                        // Sync

    

                    std::atomic<bool> syncActive{false};

    

                    std::atomic<int> syncSource{-1};

    

                

    

                    std::vector<float> scratchIn[2];



                std::vector<float> scratchOut[2];

    // Triggers
    std::vector<DeckTrigger> triggers;
    std::mutex triggerMutex;

    // Definition Tracking
    bool isDefining = false;
    std::vector<int> definedVSTIndices;
    std::vector<int> definedTriggerIDs;
    std::mutex definitionMutex;

    // VSTs moved to MixerChannel
};
    