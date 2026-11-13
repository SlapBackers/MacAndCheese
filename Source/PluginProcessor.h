/*
    MatchMaster - semi-real-time Matchering as a VST3 (GPLv3).
    Flow: load up to 3 references (A/B/C) -> Learn (listen to the live track once) -> switch profiles live.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Core/Matcher.h"
#include "Core/HyraxLimiter.h"

class MatchMasterProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numSlots = 3;
    enum class LearnState { idle, listening, analysing };

    MatchMasterProcessor();
    ~MatchMasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MatchMaster"; }
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
    void setFlavour (int slot, const juce::String&);
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
        juce::String name, flavour;
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
    std::atomic<float> suggestionDb { 0.0f }; // limiter off: gain still needed to match loudness

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Slot
    {
        std::shared_ptr<mm::ReferenceProfile> ref;
        std::shared_ptr<mm::MatchResult> result;
        juce::String name, flavour;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void matchSlots (std::vector<int> which); // worker thread
    void resmooth (double frac);              // worker thread
    void applyActiveSlot (bool forceFirs);    // message thread
    void rebuildFirs (bool force);            // message thread
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
    juce::String message;
    bool messageIsError = false;
    std::atomic<bool> resultsChanged { false }, resmoothPending { false };
    double requestedFrac = 0.0375;
    int shownSlot = -1;

    // capture of the live track during Learn
    juce::AudioBuffer<float> capture;
    std::atomic<int> capturePos { 0 };
    std::atomic<bool> capturing { false };
    std::atomic<LearnState> learnState { LearnState::idle };
    double captureRate = 44100.0;

    // real-time engine (active slot)
    double hostRate = 44100.0;
    int maxBlock = 512, firSize = 4096;
    juce::dsp::Convolution midConv, sideConv;
    juce::AudioBuffer<float> midBuf, sideBuf;
    mm::HyraxLimiter limiter;
    juce::SmoothedValue<float> loudGain, finalGain;
    std::atomic<float> activeGainDb { 0.0f }, activeRefDb { 0.0f }, activeTrackPeakDb { 0.0f }, activeEqPeakDb { 0.0f };
    std::atomic<bool> activeMatched { false };

    // exact output peak of the learned track with the current EQ settings (limiter-off headroom)
    void updateHeadroom();
    std::atomic<bool> peakJobPending { false }, exactPeakValid { false };
    std::atomic<float> exactPeakDb { 0.0f };
    juce::String lastPeakKey;
    float builtMid = -1.0f, builtSide = -1.0f, builtMaxCorr = -1.0f;
    int builtMode = -1;
    std::shared_ptr<mm::MatchResult> firSource;

    std::atomic<float>* pMatch = nullptr;
    std::atomic<float>* pMidEq = nullptr;
    std::atomic<float>* pSideEq = nullptr;
    std::atomic<float>* pLoudness = nullptr;
    std::atomic<float>* pSmoothing = nullptr;
    std::atomic<float>* pMaxCorr = nullptr;
    std::atomic<float>* pEqMode = nullptr;
    std::atomic<float>* pLimiter = nullptr;
    std::atomic<float>* pTruePeak = nullptr;
    std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pProfile = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchMasterProcessor)
};
