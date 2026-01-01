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
#include "soundtouch/BPMDetect.h"
#include <rubberband/RubberBandStretcher.h>
#include "AnalysisDB.hpp"
#include "Trigger.hpp"

class Deck {
public:
    Deck(int sampleRate = 44100);
    ~Deck();

    bool load(const std::string& filepath, AnalysisDB* db = nullptr);
    void saveAnalysis(AnalysisDB& db);

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
    
    float getBeatOffset() const { return beatOffset.load(); }
    void setBeatOffset(float o) { beatOffset.store(o); }

    bool isAnalyzing() const { return analyzing.load(); }

        void toggleMetronome() { metronomeEnabled.store(!metronomeEnabled.load()); }

        bool isMetronomeEnabled() const { return metronomeEnabled.load(); }

    

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
    bool isLoopActive() const { return loopActive.load(); }
    uint64_t getLoopStart() const { return loopStart.load(); }
    uint64_t getLoopEnd() const { return loopEnd.load(); }

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

        void analyzeBPMWork();

    

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

        std::thread analysisThread;

    

        // Metronome state (enabled/disabled)

        std::atomic<bool> metronomeEnabled{false};

        

        // File Info

        std::string currentFilepath;

        uint64_t currentFileHash = 0;

    

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
    