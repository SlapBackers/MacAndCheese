/*
    Mac and Cheese - semi-real-time Matchering as a VST3 (GPLv3).
    Flow: load up to 3 references (A/B/C) -> Learn (listen to the track once) -> live chain:
    1 fix (mono bass, Unclump) -> 2 match (M/S EQ) -> 3 loudness (clip -> gain -> limiter).
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Core/Matcher.h"
#include "Core/HyraxLimiter.h"
#include "Core/PartitionedConvolver.h"
#include "Core/ClipOnly2.h"
#include "Core/Prep.h"

class MacAndCheeseProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numSlots = 3;
    enum class LearnState { idle, listening, analysing };

    MacAndCheeseProcessor();
    ~MacAndCheeseProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mac and Cheese"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- GUI API (message thread)
    void loadReference (int slot, const juce::File&);
    void selectSlot (int slot);
    int getActiveSlot() const;
    void startLearning();
    void stopLearning();

    LearnState getLearnState() const noexcept { return learnState.load(); }
    bool hasLearnedTrack() const;
    double getCaptureSeconds() const noexcept;
    double getMaxCaptureSeconds() const noexcept;

    struct SlotInfo
    {
        juce::String name;
        bool loaded = false, busy = false, matched = false;
    };
    struct Snapshot
    {
        std::array<SlotInfo, numSlots> slots;
        std::vector<float> midCurve, sideCurve; // active slot
        double curveRate = 44100.0;
        int curveFft = 4096;
        double gainDb = 0.0, refLevelDb = 0.0;
        juce::String message;
        bool messageIsError = false;
    };
    Snapshot getSnapshot() const;

    std::atomic<float> meterL { 0.0f }, meterR { 0.0f }, meterGr { 1.0f }, maxGrDb { 0.0f };
    std::atomic<float> clipDbNow { 0.0f }, maxClipDb { 0.0f }; // dB shaved by the clipper
    std::atomic<float> suggestionDb { 0.0f }; // limiter off: gain still needed to match loudness
    std::atomic<float> gainIntoLimiterDb { 0.0f }; // clean gain between clipper and limiter
    std::atomic<float> effClipDb { 0.0f }, effCeilingDb { -1.0f }; // values in use (auto or manual)

    // Learn quality: how much the learned spectrum still changes between checks (-1 = not measured yet)
    std::atomic<float> learnDeviationDb { -1.0f };
    std::array<std::atomic<float>, mm::prepBands> bandReduction {}, bandBoost {}; // PREP: dB cut / boost per band (Bottom..Fizz)
    std::atomic<bool> learnEnough { false };

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Slot
    {
        std::shared_ptr<mm::ReferenceProfile> ref;
        std::shared_ptr<mm::MatchResult> result;
        juce::String name;
        mm::BandStats bands; // PREP statistics of this reference
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void matchSlots (std::vector<int> which); // worker thread
    void resmooth (double frac);              // worker thread
    void applyActiveSlot();                   // message thread
    void builderTick();                       // FIR builder thread: follows the knobs, independent of the GUI
    void updateLatency();
    void setMessage (const juce::String&, bool isError);
    mm::Config configFor (double frac) const;
    double currentFrac() const;

    mm::Config config;
    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };

    mutable juce::CriticalSection dataLock;
    std::array<Slot, numSlots> slots;
    std::array<std::atomic<bool>, numSlots> slotBusy {};
    std::shared_ptr<mm::TargetProfile> track;
    mm::BandStats trackBands;                 // PREP statistics of the learned track (guarded by dataLock)
    mm::Prep prep;                            // audio thread
    mm::PrepSettings prepSettings() const;
    void applyPrepStats();                    // message thread
    juce::String lastPrepKey;
    double prepChangedAt = 0.0;
    bool restoredFromProject = false; // guarded by dataLock
    juce::String message;
    bool messageIsError = false;
    std::atomic<bool> resultsChanged { false };
    int shownSlot = -1;

    // capture of the live track during Learn
    juce::AudioBuffer<float> capture;
    std::atomic<int> capturePos { 0 };
    std::atomic<bool> capturing { false };
    std::atomic<LearnState> learnState { LearnState::idle };
    double captureRate = 44100.0;
    void checkLearnQuality();               // message thread, while listening
    std::atomic<bool> learnCheckPending { false };
    struct Slice { double rmsDb = 0; std::vector<double> bands; };
    std::vector<Slice> learnSlices;         // worker thread: fixed 15 s slices of the capture
    std::vector<double> learnShape;         // worker thread: last averaged spectrum shape (dB, mean removed)
    int learnStableChecks = 0;              // worker thread

    // real-time engine (active slot)
    double hostRate = 44100.0;
    int maxBlock = 512, firSize = 4096;
    static constexpr int convBlock = 128; // partition size = added latency of the EQ engine
    mm::PartitionedConvolver midConv, sideConv;
    juce::AudioBuffer<float> midBuf, sideBuf;

    struct BuilderThread : juce::Thread
    {
        explicit BuilderThread (MacAndCheeseProcessor& p) : juce::Thread ("Mac and Cheese FIR builder"), owner (p) {}
        void run() override { while (! threadShouldExit()) { owner.builderTick(); wait (8); } }
        MacAndCheeseProcessor& owner;
    };
    BuilderThread builder { *this };
    juce::CriticalSection builderLock;  // FIR design + engine (re)configuration
    bool enginePrepared = false;        // guarded by builderLock
    std::atomic<bool> forceRebuild { true };
    void buildAndOffer (bool immediate); // with builderLock held
    mm::HyraxLimiter limiter;
    mm::ClipOnly2 clipper;
    juce::SmoothedValue<float> clipThresholdDb; // clip point, dB full scale
    float clipGainCache = -1.0f, clipInvCache = 1.0f, clipOver = 0.0f; // audio thread
    juce::SmoothedValue<float> loudGain, finalGain;
    std::atomic<float> activeGainDb { 0.0f }, activeEqLevelDb { 0.0f }, activeRefDb { 0.0f }, activeTrackPeakDb { 0.0f }, activeEqPeakDb { 0.0f };
    std::atomic<bool> activeMatched { false };

    // exact output peak of the learned track with the current EQ settings (limiter-off headroom)
    void updateHeadroom();
    std::atomic<bool> peakJobPending { false }, exactPeakValid { false };
    std::atomic<float> exactPeakDb { 0.0f };
    std::atomic<bool> exactEqLevelValid { false };
    std::atomic<float> exactEqLevelDb { 0.0f }; // level change of the current EQ settings (level-match monitor)
    juce::String lastPeakKey;
    float builtMid = -1.0f, builtSide = -1.0f, builtMaxCorr = -1.0f;
    int builtMode = -1;
    std::shared_ptr<mm::MatchResult> firSource;   // active slot's result (guarded by dataLock)
    std::shared_ptr<mm::MatchResult> builtSource; // builder thread
    int latencyMode = -1;

    std::atomic<float>* pMatch = nullptr;
    std::atomic<float>* pMidEq = nullptr;
    std::atomic<float>* pSideEq = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pGainAuto = nullptr;
    std::atomic<float>* pClipAuto = nullptr;
    std::atomic<float>* pCeilingAuto = nullptr;
    std::atomic<float>* pSmoothing = nullptr;
    std::atomic<float>* pMaxCorr = nullptr;
    std::atomic<float>* pEqMode = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pTruePeak = nullptr;
    std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pClip = nullptr;
    std::atomic<float>* pLevelMatch = nullptr;
    std::atomic<float>* pUnclump = nullptr;
    std::atomic<float>* pMonoBase = nullptr;
    std::atomic<float>* pFluff = nullptr;
    std::array<std::atomic<float>*, mm::prepBands> pBand {};
    std::atomic<float>* pProfile = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacAndCheeseProcessor)
};
